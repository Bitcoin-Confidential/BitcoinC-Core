// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2019 The Bitcoin Confidential Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pos/miner.h>

#include <pos/kernel.h>
#include <miner.h>
#include <chainparams.h>
#include <utilmoneystr.h>

#include <fs.h>
#include <sync.h>
#include <net.h>
#include <validation.h>
#include <consensus/validation.h>
#include <base58.h>
#include <crypto/sha256.h>

#include "univalue.h"
#include <wallet/hdwallet.h>

#include <stdint.h>

typedef CWallet* CWalletRef;
std::vector<StakeThread*> vStakeThreads;

void StakeThread::condWaitFor(int ms)
{
    std::unique_lock<std::mutex> lock(mtxMinerProc);
    fWakeMinerProc = false;
    condMinerProc.wait_for(lock, std::chrono::milliseconds(ms), [this] { return this->fWakeMinerProc; });
};

std::atomic<bool> fStopMinerProc(false);
std::atomic<bool> fTryToSync(false);
std::atomic<bool> fIsStaking(false);

int nMinStakeInterval = 0;  // min stake interval in seconds
int nMinerSleep = 500;
std::atomic<int64_t> nTimeLastStake(0);

extern double GetDifficulty(const CBlockIndex* blockindex = nullptr);

double GetPoSKernelPS()
{
    LOCK(cs_main);

    CBlockIndex *pindex = chainActive.Tip();
    CBlockIndex *pindexPrevStake = nullptr;

    int nBestHeight = pindex->nHeight;

    int nPoSInterval = 72; // blocks sampled
    double dStakeKernelsTriedAvg = 0;
    int nStakesHandled = 0, nStakesTime = 0;

    while (pindex && nStakesHandled < nPoSInterval) {
        if (pindex->IsProofOfStake()) {
            if (pindexPrevStake) {
                dStakeKernelsTriedAvg += GetDifficulty(pindexPrevStake) * 4294967296.0;
                nStakesTime += pindexPrevStake->nTime - pindex->nTime;
                nStakesHandled++;
            }
            pindexPrevStake = pindex;
        }
        pindex = pindex->pprev;
    }

    double result = 0;

    if (nStakesTime)  {
        result = dStakeKernelsTriedAvg / nStakesTime;
    }

    //if (IsProtocolV2(nBestHeight))
        result *= Params().GetStakeTimestampMask(nBestHeight) + 1;

    return result;
}

bool CheckStake(CBlock *pblock)
{
    uint256 proofHash, hashTarget;
    uint256 hashBlock = pblock->GetHash();

    if (!pblock->IsProofOfStake()) {
        return error("%s: %s is not a proof-of-stake block.", __func__, hashBlock.GetHex());
    }

    if (!CheckStakeUnique(*pblock, false)) { // Check in SignBlock also
        return error("%s: %s CheckStakeUnique failed.", __func__, hashBlock.GetHex());
    }

    BlockMap::const_iterator mi = mapBlockIndex.find(pblock->hashPrevBlock);
    if (mi == mapBlockIndex.end()) {
        return error("%s: %s prev block not found: %s.", __func__, hashBlock.GetHex(), pblock->hashPrevBlock.GetHex());
    }

    if (!chainActive.Contains(mi->second)) {
        return error("%s: %s prev block in active chain: %s.", __func__, hashBlock.GetHex(), pblock->hashPrevBlock.GetHex());
    }

    // Verify hash target and signature of coinstake tx
    {
        LOCK(cs_main);
        CValidationState state;
        if (!CheckProofOfStake(state, mi->second, *pblock->vtx[0], pblock->nTime, pblock->nBits, proofHash, hashTarget)) {
            return error("%s: proof-of-stake checking failed.", __func__);
        }
        if (pblock->hashPrevBlock != chainActive.Tip()->GetBlockHash()) { // hashbestchain
            return error("%s: Generated block is stale.", __func__);
        }
    }

    // debug print
    LogPrintf("CheckStake(): New proof-of-stake block found  \n  hash: %s \nproofhash: %s  \ntarget: %s\n", hashBlock.GetHex(), proofHash.GetHex(), hashTarget.GetHex());
    if (LogAcceptCategory(BCLog::POS)) {
        LogPrintf("block %s\n", pblock->ToString());
        LogPrintf("out %s\n", FormatMoney(pblock->vtx[0]->GetValueOut()));
    }

    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
    if (!ProcessNewBlock(Params(), shared_pblock, true, nullptr)) {
        return error("%s: Block not accepted.", __func__);
    }

    return true;
};

bool ImportAirdropOutputs(CBlockTemplate *pblocktemplate, int nHeight, bool fGenerateHashFile)
{
    CBlock *pblock = &pblocktemplate->block;

    if (pblock->vtx.size() < 1)
        return error("%s: Malformed block.", __func__);
    CMutableTransaction txn;

    fs::path fPath = GetDataDir() / "airdrop.txt";
    fs::path fPathOut = GetDataDir() / strprintf("airdrop_hashes_%d.txt", nHeight);

    if (!fs::exists(fPath))
        return error("%s: File not found 'airdrop.txt'.", __func__);

    const int nMaxOutputsPerTxn = 1000;
    const int nMaxTxnPerBlock = 60;

    FILE *fp;
    errno = 0;
    if (!(fp = fopen(fPath.string().c_str(), "rb")))
        return error("%s - Can't open file, strerror: %s.", __func__, strerror(errno));

    char *pRead;
    int nOutput = 0;

    while( pblock->vtx.size() < nMaxTxnPerBlock ){

        LogPrint(BCLog::POS, "%s, nHeight %d\n", __func__, nHeight);

        txn = CMutableTransaction();
        txn.nVersion = BITCOINC_TXN_VERSION;
        txn.SetType(TXN_COINBASE);
        txn.nLockTime = 0;
        txn.vin.push_back(CTxIn()); // null prevout

        // scriptsig len must be > 2
        const char *s = "smartcash_airdrop";
        txn.vin[0].scriptSig = CScript() << std::vector<unsigned char>((const unsigned char*)s, (const unsigned char*)s + strlen(s));

        int nAdded = 0;
        char cLine[512];
        char *pAddress, *pAmount;

        while ( (pRead = fgets(cLine, 512, fp)) )
        {
            cLine[511] = '\0'; // safety
            size_t len = strlen(cLine);
            while (isspace(cLine[len-1]) && len>0)
                cLine[len-1] = '\0', len--;

            if (!(pAddress = strtok(cLine, ","))
                || !(pAmount = strtok(nullptr, ",")))
                continue;

            nOutput++;
            if (nOutput <= nMaxOutputsPerTxn * (nHeight-1))
                continue;

            uint64_t amount;
            if (!ParseUInt64(std::string(pAmount), &amount) || !MoneyRange(amount))
            {
                LogPrint(BCLog::POS, "Warning: %s - Skipping invalid amount: %s, %s\n", __func__, pAmount, strerror(errno));
                continue;
            };

            std::string addrStr(pAddress);
            CSmartCashAddress addr(addrStr);

            CTxDestination dest = addr.Get();
            CScript script;

            if (!addr.IsValid())
            {
                LogPrint(BCLog::POS, "Warning: %s - Skipping invalid address: %s\n", __func__, pAddress);
                continue;
            };

            if( dest.type() == typeid(CKeyID) ){
                script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(boost::get<CKeyID>(dest)) << OP_EQUALVERIFY << OP_CHECKSIG;
            }else if( dest.type() == typeid(CScriptID) ){
                script = CScript() << OP_HASH160 << ToByteVector(boost::get<CScriptID>(dest)) << OP_EQUAL;
            }else{
                LogPrint(BCLog::POS, "Warning: %s - Skipping invalid address type: %s\n", __func__, pAddress);
                continue;
            }

            OUTPUT_PTR<CTxOutStandard> txout = MAKE_OUTPUT<CTxOutStandard>();
            txout->nValue = amount;
            txout->scriptPubKey = script;
            txn.vpout.push_back(txout);

            nAdded++;
            if (nAdded >= nMaxOutputsPerTxn)
                break;
        };

        if( txn.vpout.size() ){
            LogPrint(BCLog::POS, "%s - Add tx: %s\n", __func__, txn.GetHash().ToString());
            pblock->vtx.insert(pblock->vtx.begin()+1, MakeTransactionRef(txn));
        }else{
            break;
        }
    }

    fclose(fp);

    if( fGenerateHashFile ){
        LogPrintf("%s - Generated transactions: %d\n", __func__, pblock->vtx.size());

        if( pblock->vtx.size() ){

            if (!(fp = fopen(fPathOut.string().c_str(), "w"))){
                return error("%s - Can't create file, strerror: %s.", __func__, strerror(errno));
            }

            for( size_t i=1; i<pblock->vtx.size(); i++ ){
                std::string strOut = strprintf("vImportedCoinbaseTxns.push_back(CImportedCoinbaseTxn(%d,  uint256S(\"%s\")));\n", nHeight, pblock->vtx[i].get()->GetHash().ToString());
                fwrite(strOut.c_str(), 1, strOut.length(), fp);
            }

            fclose(fp);
        }
    }

    return fGenerateHashFile ? txn.vpout.size() > 0 : true;
}

void StartThreadStakeMiner()
{
    nMinStakeInterval = gArgs.GetArg("-minstakeinterval", 0);
    nMinerSleep = gArgs.GetArg("-minersleep", 500);

    fStopMinerProc = false;

    auto vpwallets = GetWallets();
    size_t nWallets = vpwallets.size();

    if (nWallets < 1) {
        return;
    }

    UniValue rv(UniValue::VOBJ);

    size_t nThreads = std::min(nWallets, (size_t)gArgs.GetArg("-stakingthreads", 1));
    size_t nPerThread = nWallets / nThreads;

    for (size_t i = 0; i < nThreads; ++i) {
        size_t nStart = nPerThread * i;
        size_t nEnd = (i == nThreads-1) ? nWallets : nPerThread * (i+1);

        CHDWallet *pwallet = GetBitcoinCWallet(vpwallets[i].get());
        if( gArgs.GetBoolArg("-staking", true) && pwallet->GetSetting("stakingstatus", rv) && rv.isObject() && rv.exists("enabled") && rv["enabled"].getBool()){
            StakeThread *t = new StakeThread();
            pwallet->nIsStaking = CHDWallet::NOT_STAKING_INIT;
            vStakeThreads.push_back(t);
            pwallet->nStakeThread = i;
            t->sName = strprintf("miner%d", i);
            t->thread = std::thread(&TraceThread<std::function<void()> >, t->sName.c_str(), std::function<void()>(std::bind(&ThreadStakeMiner, i, vpwallets, nStart, nEnd)));
        }else if(!gArgs.GetBoolArg("-staking", true)){
            pwallet->nIsStaking = CHDWallet::NOT_STAKING_DISABLED;
        }else{
            pwallet->nIsStaking = CHDWallet::NOT_STAKING_STOPPED;
        }
    }
}

void StopThreadStakeMiner()
{
    if (vStakeThreads.size() < 1 // no thread created
        || fStopMinerProc)
        return;
    LogPrint(BCLog::POS, "StopThreadStakeMiner\n");

    fStopMinerProc = true;
    fIsStaking = false;

    for (auto t : vStakeThreads)
    {
        {
            std::lock_guard<std::mutex> lock(t->mtxMinerProc);
            t->fWakeMinerProc = true;
        }
        t->condMinerProc.notify_all();

        t->thread.join();
        delete t;
    };
    vStakeThreads.clear();
};

void WakeThreadStakeMiner(CHDWallet *pwallet)
{
    // Call when chain is synced, wallet unlocked or balance changed
    LogPrint(BCLog::POS, "WakeThreadStakeMiner thread %d\n", pwallet->nStakeThread);

    if (pwallet->nStakeThread >= vStakeThreads.size())
        return; // stake unit test
    StakeThread *t = vStakeThreads[pwallet->nStakeThread];
    pwallet->nLastCoinStakeSearchTime = 0;
    {
        std::lock_guard<std::mutex> lock(t->mtxMinerProc);
        t->fWakeMinerProc = true;
    }

    t->condMinerProc.notify_all();
};

bool ThreadStakeMinerStopped()
{
    return fStopMinerProc;
}

static inline void condWaitFor(size_t nThreadID, int ms)
{
    assert(vStakeThreads.size() > nThreadID);
    StakeThread *t = vStakeThreads[nThreadID];
    t->condWaitFor(ms);
};

void ThreadStakeMiner(size_t nThreadID, std::vector<std::shared_ptr<CWallet>> &vpwallets, size_t nStart, size_t nEnd)
{
    LogPrintf("Starting staking thread %d, %d wallet%s.\n", nThreadID, nEnd - nStart, (nEnd - nStart) > 1 ? "s" : "");

    int nBestHeight; // TODO: set from new block signal?
    int64_t nBestTime;

    int nLastImportHeight = Params().GetLastImportHeight();

    if (!gArgs.GetBoolArg("-staking", true))
    {
        LogPrint(BCLog::POS, "%s: -staking is false.\n", __func__);
        return;
    };

    CScript coinbaseScript;
    while (!fStopMinerProc)
    {
        if (fReindex || fImporting || fBusyImporting)
        {
            fIsStaking = false;
            LogPrint(BCLog::POS, "%s: Block import/reindex.\n", __func__);
            condWaitFor(nThreadID, 30000);
            continue;
        };

        if (fTryToSync)
        {
            fTryToSync = false;

            if (g_connman->vNodes.size() < 3 || nBestHeight < GetNumBlocksOfPeers())
            {
                fIsStaking = false;
                LogPrint(BCLog::POS, "%s: TryToSync\n", __func__);
                condWaitFor(nThreadID, 2000);
                continue;
            };
        };

        if (g_connman->vNodes.empty() || IsInitialBlockDownload())
        {
            for (size_t i = nStart; i < nEnd; ++i)
            {
                auto pwallet = GetBitcoinCWallet(vpwallets[i].get());
                pwallet->nIsStaking = CHDWallet::NOT_STAKING_NOT_SYCNED;
            }

            fIsStaking = false;
            fTryToSync = true;
            LogPrint(BCLog::POS, "%s: IsInitialBlockDownload\n", __func__);
            condWaitFor(nThreadID, 2000);
            continue;
        };


        {
            LOCK(cs_main);
            nBestHeight = chainActive.Height();
            nBestTime = chainActive.Tip()->nTime;
        }

        if (nBestHeight < GetNumBlocksOfPeers()-1)
        {
            for (size_t i = nStart; i < nEnd; ++i)
            {
                auto pwallet = GetBitcoinCWallet(vpwallets[i].get());
                pwallet->nIsStaking = CHDWallet::NOT_STAKING_NOT_SYCNED;
            }

            fIsStaking = false;
            LogPrint(BCLog::POS, "%s: nBestHeight < GetNumBlocksOfPeers(), %d, %d\n", __func__, nBestHeight, GetNumBlocksOfPeers());
            condWaitFor(nThreadID, nMinerSleep * 4);
            continue;
        };

        if (nMinStakeInterval > 0 && nTimeLastStake + (int64_t)nMinStakeInterval > GetTime())
        {
            LogPrint(BCLog::POS, "%s: Rate limited to 1 / %d seconds.\n", __func__, nMinStakeInterval);
            condWaitFor(nThreadID, nMinStakeInterval * 500); // nMinStakeInterval / 2 seconds
            continue;
        };

        int64_t nTime = GetAdjustedTime();
        int64_t nMask = Params().GetStakeTimestampMask(nBestHeight+1);
        int64_t nSearchTime = nTime & ~nMask;
        if (nSearchTime <= nBestTime)
        {
            if (nTime < nBestTime)
            {
                LogPrint(BCLog::POS, "%s: Can't stake before last block time.\n", __func__);
                condWaitFor(nThreadID, std::min(1000 + (nBestTime - nTime) * 1000, (int64_t)30000));
                continue;
            };

            int64_t nNextSearch = nSearchTime + nMask;
            condWaitFor(nThreadID, std::min(nMinerSleep + (nNextSearch - nTime) * 1000, (int64_t)10000));
            continue;
        };

        std::unique_ptr<CBlockTemplate> pblocktemplate;

        size_t nWaitFor = 60000;
        for (size_t i = nStart; i < nEnd; ++i)
        {
            auto pwallet = GetBitcoinCWallet(vpwallets[i].get());

            if (!pwallet->fStakingEnabled)
            {
                pwallet->nIsStaking = CHDWallet::NOT_STAKING_DISABLED;
                continue;
            };

            if (nSearchTime <= pwallet->nLastCoinStakeSearchTime)
            {
                nWaitFor = std::min(nWaitFor, (size_t)nMinerSleep);
                continue;
            };

            if (pwallet->nStakeLimitHeight && nBestHeight >= pwallet->nStakeLimitHeight)
            {
                pwallet->nIsStaking = CHDWallet::NOT_STAKING_LIMITED;
                nWaitFor = std::min(nWaitFor, (size_t)30000);
                continue;
            };

            if (pwallet->IsLocked())
            {
                pwallet->nIsStaking = CHDWallet::NOT_STAKING_LOCKED;
                nWaitFor = std::min(nWaitFor, (size_t)30000);
                continue;
            };

            if (pwallet->GetSpendableBalance() <= pwallet->nReserveBalance)
            {
                pwallet->nIsStaking = CHDWallet::NOT_STAKING_BALANCE;
                nWaitFor = std::min(nWaitFor, (size_t)60000);
                pwallet->nLastCoinStakeSearchTime = nSearchTime + 60;
                LogPrint(BCLog::POS, "%s: Wallet %d, low balance.\n", __func__, i);
                continue;
            };

            if (!pblocktemplate.get())
            {
                pblocktemplate = BlockAssembler(Params()).CreateNewBlock(coinbaseScript, true, false);
                if (!pblocktemplate.get())
                {
                    fIsStaking = false;
                    nWaitFor = std::min(nWaitFor, (size_t)nMinerSleep);
                    LogPrint(BCLog::POS, "%s: Couldn't create new block.\n", __func__);
                    continue;
                };

                if (nBestHeight+1 <= nLastImportHeight
                    && !ImportAirdropOutputs(pblocktemplate.get(), nBestHeight+1, false))
                {
                    fIsStaking = false;
                    nWaitFor = std::min(nWaitFor, (size_t)30000);
                    LogPrint(BCLog::POS, "%s: ImportOutputs failed.\n", __func__);
                    continue;
                };
            };

            pwallet->nIsStaking = CHDWallet::IS_STAKING;
            nWaitFor = nMinerSleep;
            fIsStaking = true;
            if (pwallet->SignBlock(pblocktemplate.get(), nBestHeight+1, nSearchTime))
            {
                CBlock *pblock = &pblocktemplate->block;
                if (CheckStake(pblock))
                {
                     nTimeLastStake = GetTime();
                     break;
                };
            } else
            {
                int nRequiredDepth = std::min((int)(Params().GetStakeMinConfirmations()-1), (int)(nBestHeight / 2));
                if (pwallet->m_greatest_txn_depth < nRequiredDepth-4)
                {
                    pwallet->nIsStaking = CHDWallet::NOT_STAKING_DEPTH;
                    size_t nSleep = (nRequiredDepth - pwallet->m_greatest_txn_depth) / 4;
                    nWaitFor = std::min(nWaitFor, (size_t)(nSleep * 1000));
                    pwallet->nLastCoinStakeSearchTime = nSearchTime + nSleep;
                    LogPrint(BCLog::POS, "%s: Wallet %d, no outputs with required depth, sleeping for %ds.\n", __func__, i, nSleep);
                    continue;
                };
            };
        };

        condWaitFor(nThreadID, nWaitFor);
    };
};


void GenerateAirdropHashes()
{
    int nHeight = 1;

    CScript coinbaseScript;
    CBlockTemplate pblocktemplate;
    pblocktemplate.block.vtx.resize(1);

    while( ImportAirdropOutputs(&pblocktemplate, nHeight, true) ){
        LogPrintf("Generated block %d with %d transactions", nHeight++, pblocktemplate.block.vtx.size());
        pblocktemplate.block.vtx.clear();
        pblocktemplate.block.vtx.resize(1);
    }
}
