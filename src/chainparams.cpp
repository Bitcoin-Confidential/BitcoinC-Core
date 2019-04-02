// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019 The Bitcoin Confidential developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/merkle.h>

#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>
#include <utilmoneystr.h>

#include <assert.h>

#include <chainparamsseeds.h>
#include <chainparamsimport.h>

int64_t CChainParams::GetCoinYearReward(int64_t nTime) const
{
    static const int64_t nSecondsInYear = 365 * 24 * 60 * 60;

    if (strNetworkID != "regtest")
    {
        // Y1 5%, Y2 4%, Y3 3%, Y4 2%, Y5-10 1%, Y10-15 0.5%, Y15-25 .2%, Y25-50 0.1%, Y50 fees only
        int64_t nYearsSinceGenesis = (nTime - genesis.nTime) / nSecondsInYear;

        if (nYearsSinceGenesis >= 0 && nYearsSinceGenesis < 5)
            return (5 - nYearsSinceGenesis) * CENT;
        if (nYearsSinceGenesis >= 5 && nYearsSinceGenesis < 10)
            return 1 * CENT;
        if (nYearsSinceGenesis >= 10 && nYearsSinceGenesis < 15)
            return 0.5 * CENT;
        if (nYearsSinceGenesis >= 15 && nYearsSinceGenesis < 25)
            return 0.2 * CENT;
        if (nYearsSinceGenesis >= 25 && nYearsSinceGenesis < 50)
            return 0.1 * CENT;
    };

    return nCoinYearReward;
}

int64_t CChainParams::GetProofOfStakeReward(const CBlockIndex *pindexPrev, int64_t nFees) const
{
    int64_t nSubsidy;

    nSubsidy = (pindexPrev->nMoneySupply / COIN) * GetCoinYearReward(pindexPrev->nTime) / (365 * 24 * (60 * 60 / nTargetSpacing));

    if( pindexPrev->nHeight < Params().GetLaunchPhaseEndHeight() ){
        nSubsidy *= Params().GetLaunchPhaseRewardRatio();
    }

    if (LogAcceptCategory(BCLog::POS) && gArgs.GetBoolArg("-printcreation", false))
        LogPrintf("GetProofOfStakeReward(): create=%s\n", FormatMoney(nSubsidy).c_str());

    return nSubsidy + nFees;
};

bool CChainParams::CheckAirdropCoinbase( const CBlock *pblock, int nHeight) const
{

    int nExpected = 0, nFound = 0;
    for (auto &cth : Params().vImportedCoinbaseTxns)
    {
        if (cth.nHeight != (uint32_t)nHeight)
            continue;

        ++nExpected;

        bool fTxFound = false;
        for( size_t i=0;i<pblock->vtx.size(); i++){
            if( pblock->vtx[i].get()->GetHash() == cth.hash && pblock->vtx[i].get()->IsCoinBase() ){
                ++nFound;
                fTxFound = true;
            }
        }

        if (!fTxFound)
            return error("%s - Airdrop tx missing at height %d: expect tx %s.", __func__, nHeight, cth.hash.ToString());
    }

    return nFound == nExpected;
}

const DevFundSettings *CChainParams::GetDevFundSettings(int64_t nTime) const
{
    for (size_t i = vDevFundSettings.size(); i-- > 0; )
    {
        if (nTime > vDevFundSettings[i].first)
            return &vDevFundSettings[i].second;
    };

    return nullptr;
};

bool CChainParams::IsBech32Prefix(const std::vector<unsigned char> &vchPrefixIn) const
{
    for (auto &hrp : bech32Prefixes)  {
        if (vchPrefixIn == hrp) {
            return true;
        }
    }

    return false;
};

bool CChainParams::IsBech32Prefix(const std::vector<unsigned char> &vchPrefixIn, CChainParams::Base58Type &rtype) const
{
    for (size_t k = 0; k < MAX_BASE58_TYPES; ++k) {
        auto &hrp = bech32Prefixes[k];
        if (vchPrefixIn == hrp) {
            rtype = static_cast<CChainParams::Base58Type>(k);
            return true;
        }
    }

    return false;
};

bool CChainParams::IsBech32Prefix(const char *ps, size_t slen, CChainParams::Base58Type &rtype) const
{
    for (size_t k = 0; k < MAX_BASE58_TYPES; ++k)
    {
        const auto &hrp = bech32Prefixes[k];
        size_t hrplen = hrp.size();
        if (hrplen > 0
            && slen > hrplen
            && strncmp(ps, (const char*)&hrp[0], hrplen) == 0)
        {
            rtype = static_cast<CChainParams::Base58Type>(k);
            return true;
        };
    };

    return false;
};

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/*
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
*/
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "NY Times 12/12/2018 Who's Living in a 'Bubble'";
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

//RegTest
const std::pair<const char*, CAmount> regTestOutputs[] = {
//  std::make_pair("585c2b3914d9ee51f8e710304e386531c3abcc82", 10000 * COIN),
};

const size_t nGenesisOutputsRegtest = sizeof(regTestOutputs) / sizeof(regTestOutputs[0]);

//Mainnet
const std::pair<const char*, CAmount> genesisOutputs[] = {
         //Dev address bc4iXxHcHUsMJxcW8s2EReF5ErtmhwuuxZ
    std::make_pair("fff85dd28a712f821414fabec2a58395858ff7ae",1000 * COIN),
};

const size_t nGenesisOutputs = sizeof(genesisOutputs) / sizeof(genesisOutputs[0]);

//Testnet
const std::pair<const char*, CAmount> genesisOutputsTestnet[] = {
    //Staking test 1 pqjn7jnXdMfov8JkYaF6yEMyM8nP95LQVy
    std::make_pair("e4c40e40a13e0d412d43ccf3a9116cd9c4e9862e",1000 * COIN),
};

const size_t nGenesisOutputsTestnet = sizeof(genesisOutputsTestnet) / sizeof(genesisOutputsTestnet[0]);

static CBlock CreateGenesisBlockRegTest(uint32_t nTime, uint32_t nNonce, uint32_t nBits)
{
    const char *pszTimestamp = "NY Times 12/12/2018 Who's Living in a 'Bubble'";

    CMutableTransaction txNew;
    txNew.nVersion = BITCOINC_TXN_VERSION;
    txNew.SetType(TXN_COINBASE);
    txNew.vin.resize(1);
    uint32_t nHeight = 0;  // bip34
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp)) << OP_RETURN << nHeight;

    txNew.vpout.resize(nGenesisOutputsRegtest);
    for (size_t k = 0; k < nGenesisOutputsRegtest; ++k)
    {
        OUTPUT_PTR<CTxOutStandard> out = MAKE_OUTPUT<CTxOutStandard>();
        out->nValue = regTestOutputs[k].second;
        out->scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex(regTestOutputs[k].first) << OP_EQUALVERIFY << OP_CHECKSIG;
        txNew.vpout[k] = out;
    };

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = BITCOINC_BLOCK_VERSION;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));

    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    genesis.hashWitnessMerkleRoot = BlockWitnessMerkleRoot(genesis);

    return genesis;
}

static CBlock CreateGenesisBlockTestNet(uint32_t nTime, uint32_t nNonce, uint32_t nBits)
{
    const char *pszTimestamp = "NY Times 12/12/2018 Who's Living in a 'Bubble'";

    CMutableTransaction txNew;
    txNew.nVersion = BITCOINC_TXN_VERSION;
    txNew.SetType(TXN_COINBASE);
    txNew.vin.resize(1);
    uint32_t nHeight = 0;  // bip34
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp)) << OP_RETURN << nHeight;

    txNew.vpout.resize(nGenesisOutputsTestnet);
    for (size_t k = 0; k < nGenesisOutputsTestnet; ++k)
    {
        OUTPUT_PTR<CTxOutStandard> out = MAKE_OUTPUT<CTxOutStandard>();
        out->nValue = genesisOutputsTestnet[k].second;
        out->scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex(genesisOutputsTestnet[k].first) << OP_EQUALVERIFY << OP_CHECKSIG;
        txNew.vpout[k] = out;
    };

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = BITCOINC_BLOCK_VERSION;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));

    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    genesis.hashWitnessMerkleRoot = BlockWitnessMerkleRoot(genesis);

    return genesis;
}

static CBlock CreateGenesisBlockMainNet(uint32_t nTime, uint32_t nNonce, uint32_t nBits)
{
    const char *pszTimestamp = "NY Times 12/12/2018 Who's Living in a 'Bubble'";

    CMutableTransaction txNew;
    txNew.nVersion = BITCOINC_TXN_VERSION;
    txNew.SetType(TXN_COINBASE);

    txNew.vin.resize(1);
    uint32_t nHeight = 0;  // bip34
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp)) << OP_RETURN << nHeight;

    txNew.vpout.resize(nGenesisOutputs);
    for (size_t k = 0; k < nGenesisOutputs; ++k) {
        OUTPUT_PTR<CTxOutStandard> out = MAKE_OUTPUT<CTxOutStandard>();
        out->nValue = genesisOutputs[k].second;
        out->scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex(genesisOutputs[k].first) << OP_EQUALVERIFY << OP_CHECKSIG;
        txNew.vpout[k] = out;
    }

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = BITCOINC_BLOCK_VERSION;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));

    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    genesis.hashWitnessMerkleRoot = BlockWitnessMerkleRoot(genesis);

    return genesis;
}

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";

        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 0;
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.OpIsCoinstakeTime = 1510272000; // 2017-11-10 00:00:00 UTC
        consensus.fAllowOpIsCoinstakeWithP2PKH = false;
        consensus.nPaidSmsgTime = 0x3AFE130E00; // 9999 TODO: lower
        consensus.csp2shHeight = 0x7FFFFFFF;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 2 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1462060800; // May 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1479168000; // November 15th, 2016.
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1510704000; // November 15th, 2017.

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
  //      consensus.defaultAssumeValid = uint256S("0x5dacfb4e040ed98031f8586ce1516b6813990b53d677fb45a49099e8ceecb6fa"); //250000

        consensus.nMinRCTOutputDepth = 12;

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xbc;
        pchMessageStart[1] = 0x15;
        pchMessageStart[2] = 0xc0;
        pchMessageStart[3] = 0x01;
        nDefaultPort = 9789;
        nBIP44ID = 0x8000002C;

        nModifierInterval = 10 * 60;    // 10 minutes
        nStakeMinConfirmations = 225;   // 225 * 2 minutes
        nTargetSpacing = 120;           // 2 minutes
        nTargetTimespan = 24 * 60;      // 24 mins

        AddImportHashesMain(vImportedCoinbaseTxns);
        SetLastImportHeight();

        nLaunchPhaseEndHeight = 5040; // 7 days
        dLaunchPhaseRewardRatio = 0.05; // 5% of the actual reward

        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlockMainNet(1551128292, 36140, 0x1f00ffff); // 2018-12-12
        consensus.hashGenesisBlock = genesis.GetHash();
/*
            bool fNegative;
            bool fOverflow;
            arith_uint256 bnTarget;

            bnTarget.SetCompact(0x1f00ffff, &fNegative, &fOverflow);
                  for(int count = 1; count < 100000000000; count++){
                     genesis = CreateGenesisBlockMainNet(1551128292, count, 0x1f00ffff);
    // default
                     if (UintToArith256(genesis.GetHash()) <= bnTarget){
                        printf("new mainnet genesis.nNonce = %d\n", count);
                        printf("new mainnet genesis hash: %s\n", genesis.GetHash().ToString().c_str());
                        printf("new mainnet genesis merkle root: %s\n", genesis.hashMerkleRoot.ToString().c_str());
                        printf("new mainnet genesis Witness merkle root: %s\n", genesis.hashWitnessMerkleRoot.ToString().c_str());
                        break;
                    }
                     if( count % 1000000 == 0 )
                      printf("count = %d\n", count);
                     }
*/
        assert(consensus.hashGenesisBlock == uint256S("0x0000c1bed1b5d0905a9e0629859444c8102d220f79d49d38806fcfc3b5fbdea1"));
        assert(genesis.hashMerkleRoot == uint256S("0x55f4623c823f63ea4bbdb678de0fcbacc95eef94b679a1bb8ca2b89d1049a053"));
        assert(genesis.hashWitnessMerkleRoot == uint256S("0x88cac0b7df946812ec8fcbbb8a9547151b070067c5417c84a0247329d93d7624"));

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        vSeeds.emplace_back("mainnet.b-c.rocks");
        vSeeds.emplace_back("mainnet-seed.bitcoinconfidential.cc");
        vSeeds.emplace_back("dnsseed-mainnet.bitcoinconfidential.cc");
        vSeeds.emplace_back("mainnet.bitcoinconfidential.cc");

        vDevFundSettings.emplace_back(0,DevFundSettings("bc4iXxHcHUsMJxcW8s2EReF5ErtmhwuuxZ", 30, 1000));

        base58Prefixes[PUBKEY_ADDRESS]     = {0x55}; // b ColdStake
        base58Prefixes[SCRIPT_ADDRESS]     = {0x0A}; // 5
        base58Prefixes[PUBKEY_ADDRESS_256] = {0x99}; // 6 Contract
        base58Prefixes[SCRIPT_ADDRESS_256] = {0x3d}; // 7
        base58Prefixes[SECRET_KEY]         = {0x4B}; // C
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x02, 0xD4, 0x13, 0xFF}; // bpub
        base58Prefixes[EXT_SECRET_KEY]     = {0x02, 0xD4, 0x0F, 0xC5}; // bprv

        base58Prefixes[STEALTH_ADDRESS]    = {0x08}; // B
        base58Prefixes[EXT_KEY_HASH]       = {0x4b}; // X
        base58Prefixes[EXT_ACC_HASH]       = {0x17}; // A
        base58Prefixes[EXT_PUBLIC_KEY_BTC] = {0x04, 0x88, 0xB2, 0x1E}; // xpub
        base58Prefixes[EXT_SECRET_KEY_BTC] = {0x04, 0x88, 0xAD, 0xE4}; // xprv

        base58Prefixes[SC_PUBKEY_ADDRESS]     = {0x3F};
        base58Prefixes[SC_SCRIPT_ADDRESS]     = {0x12};
        base58Prefixes[SC_SECRET_KEY]         = {0xBF};

        bech32Prefixes[PUBKEY_ADDRESS].assign       ("bh","bh"+2);
        bech32Prefixes[SCRIPT_ADDRESS].assign       ("br","br"+2);
        bech32Prefixes[PUBKEY_ADDRESS_256].assign   ("bl","bl"+2);
        bech32Prefixes[SCRIPT_ADDRESS_256].assign   ("bj","bj"+2);
        bech32Prefixes[SECRET_KEY].assign           ("bx","bx"+2);
        bech32Prefixes[EXT_PUBLIC_KEY].assign       ("bep","bep"+3);
        bech32Prefixes[EXT_SECRET_KEY].assign       ("bex","bex"+3);
        bech32Prefixes[STEALTH_ADDRESS].assign      ("bs","bs"+2);
        bech32Prefixes[EXT_KEY_HASH].assign         ("bek","bek"+3);
        bech32Prefixes[EXT_ACC_HASH].assign         ("bea","bea"+3);
        bech32Prefixes[STAKE_ONLY_PKADDR].assign    ("bcs","bcs"+3);

        bech32_hrp = "bc";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;

        checkpointData = {
            {
  //              { 0,     uint256S("0x004ec413e6e46a81882ca363fe14832b872d594c9d543122b557fcfb7b433b95")}
  //              { 15000,    uint256S("0xafc73ac299f2e6dd309077d230fccef547b9fc24379c1bf324dd3683b13c61c3")},
            }
        };

        /* disable fallback fee on mainnet */
        m_fallback_fee_enabled = false;
    }

    void SetOld()
    {
        consensus.BIP16Exception = uint256S("0x00000000000002dc756eebf4f49723ed8d30cc28a5f108eb94b1ba88ac4f9c22");
//        consensus.BIP34Height = 1;
//        consensus.BIP34Hash = uint256S("0x000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b8");
        consensus.BIP65Height = 1; // 000000000000000004c2b624ed5d7756c508d90fd0da2c7c679febfa6c4735f0
        consensus.BIP66Height = 1; // 00000000000000000379eaa19dce8c9b722d46ae6a57c2f1a988119488b50931
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        genesis = CreateGenesisBlock(1231006505, 2083236893, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 0;
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.OpIsCoinstakeTime = 0;
        consensus.fAllowOpIsCoinstakeWithP2PKH = false;
        consensus.nPaidSmsgTime = 0;
        consensus.csp2shHeight = 0x7FFFFFFF;

        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 30;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1456790400; // March 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1462060800; // May 1st 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1493596800; // May 1st 2017

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        //consensus.defaultAssumeValid = uint256S("0x000000000871ee6842d3648317ccc8a435eb8cc3c2429aee94faff9ba26b05a0"); //1043841

        consensus.nMinRCTOutputDepth = 12;

        pchMessageStart[0] = 0xbc;
        pchMessageStart[1] = 0x15;
        pchMessageStart[2] = 0xab;
        pchMessageStart[3] = 0x1e;
        nDefaultPort = 19789;
        nBIP44ID = 0x80000001;

        nModifierInterval = 1 * 60;    // was 10 minutes
        nStakeMinConfirmations = 225;   // 225 * 2 minutes
        nTargetSpacing = 30;           // 30 seconds was 120 seconds
        nTargetTimespan = 24 * 60;      // 24 mins

        AddImportHashesTest(vImportedCoinbaseTxns);

        SetLastImportHeight();

        nLaunchPhaseEndHeight = 20;
        dLaunchPhaseRewardRatio = 0.05; // 5% of the actual reward

        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlockTestNet(1553477363, 21886, 0x1f00ffff);
        consensus.hashGenesisBlock = genesis.GetHash();
/*
           bool fNegative;
           bool fOverflow;
           arith_uint256 bnTarget;

           bnTarget.SetCompact(0x1f00ffff, &fNegative, &fOverflow);
                 for(int count = 1; count < 100000000000; count++){
                    genesis = CreateGenesisBlockTestNet(1553477363, count, 0x1f00ffff);
   // default
                    if (UintToArith256(genesis.GetHash()) <= bnTarget){
                       printf("testnet genesis.nNonce = %d\n", count);
                       printf("new testnet genesis merkle root: %s\n", genesis.hashMerkleRoot.ToString().c_str());
                       printf("new testnet genesis Witness merkle root: %s\n", genesis.hashWitnessMerkleRoot.ToString().c_str());
                       printf("new testnet genesis hash: %s\n", genesis.GetHash().ToString().c_str());
                       break;
                   }
                    if( count % 1000000 == 0 )
                     printf("count = %d\n", count);
                    }
*/
        assert(genesis.hashMerkleRoot == uint256S("0xb52c7733c105ad905317d0b19b484aa94f8a80266cafc4a35452c018ceb0fcba"));
        assert(genesis.hashWitnessMerkleRoot == uint256S("0x521af469e7f698c81551a3e81e6096d52bdc1880ecd73e31bed8430cbeb199e5"));
        assert(consensus.hashGenesisBlock == uint256S("0x0000d7129ea60ca9e251546298ca7bb36b7a1e00c149dade2b8efe89fef14ed7"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        vSeeds.emplace_back("testnet.b-c.rocks");
        vSeeds.emplace_back("testnet-seed.bitcoinconfidential.cc");
        vSeeds.emplace_back("dnsseed-testnet.bitcoinconfidential.cc");

//        vDevFundSettings.push_back(std::make_pair(0, DevFundSettings("pYf3vP9nqyTVrpnsqibvfn9rFMXRHRCgcc", 30, 1)));
        vDevFundSettings.emplace_back(std::make_pair(0, DevFundSettings("pYf3vP9nqyTVrpnsqibvfn9rFMXRHRCgcc", 30, 10)));

        base58Prefixes[PUBKEY_ADDRESS]     = {0x76}; // p
        base58Prefixes[SCRIPT_ADDRESS]     = {0x7a};
        base58Prefixes[PUBKEY_ADDRESS_256] = {0x77};
        base58Prefixes[SCRIPT_ADDRESS_256] = {0x7b};
        base58Prefixes[SECRET_KEY]         = {0x2e};
        base58Prefixes[EXT_PUBLIC_KEY]     = {0xe1, 0x42, 0x78, 0x00}; // ppar
        base58Prefixes[EXT_SECRET_KEY]     = {0x04, 0x88, 0x94, 0x78}; // xpar
        base58Prefixes[STEALTH_ADDRESS]    = {0x15}; // T
        base58Prefixes[EXT_KEY_HASH]       = {0x89}; // x
        base58Prefixes[EXT_ACC_HASH]       = {0x53}; // a
        base58Prefixes[EXT_PUBLIC_KEY_BTC] = {0x04, 0x35, 0x87, 0xCF}; // tpub
        base58Prefixes[EXT_SECRET_KEY_BTC] = {0x04, 0x35, 0x83, 0x94}; // tprv

        base58Prefixes[SC_PUBKEY_ADDRESS]     = {0x3F};
        base58Prefixes[SC_SCRIPT_ADDRESS]     = {0x12};
        base58Prefixes[SC_SECRET_KEY]         = {0xBF};

        bech32Prefixes[PUBKEY_ADDRESS].assign       ("tph","tph"+3);
        bech32Prefixes[SCRIPT_ADDRESS].assign       ("tpr","tpr"+3);
        bech32Prefixes[PUBKEY_ADDRESS_256].assign   ("tpl","tpl"+3);
        bech32Prefixes[SCRIPT_ADDRESS_256].assign   ("tpj","tpj"+3);
        bech32Prefixes[SECRET_KEY].assign           ("tpx","tpx"+3);
        bech32Prefixes[EXT_PUBLIC_KEY].assign       ("tpep","tpep"+4);
        bech32Prefixes[EXT_SECRET_KEY].assign       ("tpex","tpex"+4);
        bech32Prefixes[STEALTH_ADDRESS].assign      ("tps","tps"+3);
        bech32Prefixes[EXT_KEY_HASH].assign         ("tpek","tpek"+4);
        bech32Prefixes[EXT_ACC_HASH].assign         ("tpea","tpea"+4);
        bech32Prefixes[STAKE_ONLY_PKADDR].assign    ("tpcs","tpcs"+4);

        bech32_hrp = "tb";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;

        checkpointData = {
            {
/*              {127620, uint256S("0xe5ab909fc029b253bad300ccf859eb509e03897e7853e8bfdde2710dbf248dd1")},
                {210920, uint256S("0x5534f546c3b5a264ca034703b9694fabf36d749d66e0659eef5f0734479b9802")},
                {259290, uint256S("0x58267bdf935a2e0716cb910d055b8cdaa019089a5f71c3db90765dc7101dc5dc")},
*/            }
        };

        chainTxData = ChainTxData{
            // Data from rpc: getchaintxstats 4096 58267bdf935a2e0716cb910d055b8cdaa019089a5f71c3db90765dc7101dc5dc
  //          /* nTime    */ 1539418544,
    //        /* nTxCount */ 278613,
      //      /* dTxRate  */ 0.005
        };

        /* enable fallback fee on testnet */
        m_fallback_fee_enabled = true;
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP16Exception = uint256();
        consensus.BIP34Height = 100000000; // BIP34 has not activated on regtest (far in the future so block v1 are not rejected in tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in rpc activation tests)
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in rpc activation tests)
        consensus.OpIsCoinstakeTime = 0;
        consensus.fAllowOpIsCoinstakeWithP2PKH = false;
        consensus.nPaidSmsgTime = 0;
        consensus.csp2shHeight = 0;

        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 2 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // The best chain should have at least this much work.
//        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
//        consensus.defaultAssumeValid = uint256S("0x00");

        consensus.nMinRCTOutputDepth = 1;

        pchMessageStart[0] = 0xbc;
        pchMessageStart[1] = 0x5c;
        pchMessageStart[2] = 0xa1;
        pchMessageStart[3] = 0xe5;
        nDefaultPort = 11938;
        nBIP44ID = 0x80000001;


        nModifierInterval = 2 * 60;     // 2 minutes
        nStakeMinConfirmations = 12;
        nTargetSpacing = 5;             // 5 seconds
        nTargetTimespan = 16 * 60;      // 16 mins
        nStakeTimestampMask = 0;

        SetLastImportHeight();

        nLaunchPhaseEndHeight = 5040; // 7 days
        dLaunchPhaseRewardRatio = 0.05; // 5% of the actual reward

        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlockRegTest(1544595191, 4, 0x207fffff);
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("0x276d721ff1b46eabc60df2eb403d1a2f78aa90fa57cb795028060965024e1455"));
        assert(genesis.hashMerkleRoot == uint256S("0xe2c6f30c2cc6ea4d516f889a03a5a0468ba567218fa13a09ec18836d35fc83e0"));
        assert(genesis.hashWitnessMerkleRoot == uint256S("0xd833c75ac31f70bc526f6da2ec443825e04a282f5bded429b2e042bd8f08fd66"));


        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;

        checkpointData = {
            {
  //              {0, uint256S("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")},
            }
        };

        base58Prefixes[PUBKEY_ADDRESS]     = {0x76}; // p
        base58Prefixes[SCRIPT_ADDRESS]     = {0x7a};
        base58Prefixes[PUBKEY_ADDRESS_256] = {0x77};
        base58Prefixes[SCRIPT_ADDRESS_256] = {0x7b};
        base58Prefixes[SECRET_KEY]         = {0x2e};
        base58Prefixes[EXT_PUBLIC_KEY]     = {0xe1, 0x42, 0x78, 0x00}; // ppar
        base58Prefixes[EXT_SECRET_KEY]     = {0x04, 0x88, 0x94, 0x78}; // xpar
        base58Prefixes[STEALTH_ADDRESS]    = {0x15}; // T
        base58Prefixes[EXT_KEY_HASH]       = {0x89}; // x
        base58Prefixes[EXT_ACC_HASH]       = {0x53}; // a
        base58Prefixes[EXT_PUBLIC_KEY_BTC] = {0x04, 0x35, 0x87, 0xCF}; // tpub
        base58Prefixes[EXT_SECRET_KEY_BTC] = {0x04, 0x35, 0x83, 0x94}; // tprv

        base58Prefixes[SC_PUBKEY_ADDRESS]     = {0x3F};
        base58Prefixes[SC_SCRIPT_ADDRESS]     = {0x12};
        base58Prefixes[SC_SECRET_KEY]         = {0xBF};

        bech32Prefixes[PUBKEY_ADDRESS].assign       ("tph","tph"+3);
        bech32Prefixes[SCRIPT_ADDRESS].assign       ("tpr","tpr"+3);
        bech32Prefixes[PUBKEY_ADDRESS_256].assign   ("tpl","tpl"+3);
        bech32Prefixes[SCRIPT_ADDRESS_256].assign   ("tpj","tpj"+3);
        bech32Prefixes[SECRET_KEY].assign           ("tpx","tpx"+3);
        bech32Prefixes[EXT_PUBLIC_KEY].assign       ("tpep","tpep"+4);
        bech32Prefixes[EXT_SECRET_KEY].assign       ("tpex","tpex"+4);
        bech32Prefixes[STEALTH_ADDRESS].assign      ("tps","tps"+3);
        bech32Prefixes[EXT_KEY_HASH].assign         ("tpek","tpek"+4);
        bech32Prefixes[EXT_ACC_HASH].assign         ("tpea","tpea"+4);
        bech32Prefixes[STAKE_ONLY_PKADDR].assign    ("tpcs","tpcs"+4);

        bech32_hrp = "bcrt";

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        /* enable fallback fee on regtest */
        m_fallback_fee_enabled = true;
    }

    void SetOld()
    {
        genesis = CreateGenesisBlock(1296688602, 2, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

const CChainParams *pParams() {
    return globalChainParams.get();
};

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}


void SetOldParams(std::unique_ptr<CChainParams> &params)
{
    if (params->NetworkID() == CBaseChainParams::MAIN)
        return ((CMainParams*)params.get())->SetOld();
    if (params->NetworkID() == CBaseChainParams::REGTEST)
        return ((CRegTestParams*)params.get())->SetOld();
};

void ResetParams(std::string sNetworkId, bool fBitcoinCModeIn)
{
    // Hack to pass old unit tests
    globalChainParams = CreateChainParams(sNetworkId);
    if (!fBitcoinCModeIn)
    {
        SetOldParams(globalChainParams);
    };
};

/**
 * Mutable handle to regtest params
 */
CChainParams &RegtestParams()
{
    return *globalChainParams.get();
};

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout);
}
