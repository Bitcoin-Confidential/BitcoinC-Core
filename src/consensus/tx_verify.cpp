// Copyright (c) 2017-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_verify.h>

#include <consensus/consensus.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <consensus/validation.h>
#include <validation.h>
#include <chainparams.h>

#include <blind.h>
#include <anon.h>
#include <timedata.h>
#include <util.h>


// TODO remove the following dependencies
#include <chain.h>
#include <coins.h>
#include <utilmoneystr.h>


#include <policy/policy.h>
#include <smsg/smessage.h>


extern bool fBusyImporting;
extern bool fSkipRangeproof;

bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    if (tx.nLockTime == 0)
        return true;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for (const auto& txin : tx.vin) {
        if (!(txin.nSequence == CTxIn::SEQUENCE_FINAL))
            return false;
    }
    return true;
}

std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    assert(prevHeights->size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // tx.nVersion is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nVersion
    // wouldn't support BIP 68.
    bool fEnforceBIP68 = static_cast<uint32_t>(tx.nVersion) >= 2
                      && flags & LOCKTIME_VERIFY_SEQUENCE;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68) {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
        const CTxIn& txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.IsAnonInput()
            || txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            // The height of this input is not relevant for sequence locks
            (*prevHeights)[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = (*prevHeights)[txinIndex];

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight-1, 0))->GetMedianTimePast();
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair)
{
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

unsigned int GetLegacySigOpCount(const CTransaction& tx)
{
    unsigned int nSigOps = 0;
    if (!tx.IsBitcoinCVersion())
    {
        for (const auto& txin : tx.vin)
        {
            nSigOps += txin.scriptSig.GetSigOpCount(false);
        }
        for (const auto& txout : tx.vout)
        {
            nSigOps += txout.scriptPubKey.GetSigOpCount(false);
        }
    }
    for (const auto &txout : tx.vpout)
    {
        const CScript *pScriptPubKey = txout->GetPScriptPubKey();
        if (pScriptPubKey)
            nSigOps += pScriptPubKey->GetSigOpCount(false);
    };
    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs)
{
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        if (tx.vin[i].IsAnonInput())
            continue;

        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        if (prevout.scriptPubKey.IsPayToScriptHashAny(tx.IsCoinStake()))
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
    }
    return nSigOps;
}

int64_t GetTransactionSigOpCost(const CTransaction& tx, const CCoinsViewCache& inputs, int flags)
{
    int64_t nSigOps = GetLegacySigOpCount(tx) * WITNESS_SCALE_FACTOR;

    if (tx.IsCoinBase())
        return nSigOps;

    if (flags & SCRIPT_VERIFY_P2SH) {
        nSigOps += GetP2SHSigOpCount(tx, inputs) * WITNESS_SCALE_FACTOR;
    }

    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        if (tx.vin[i].IsAnonInput())
            continue;

        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        nSigOps += CountWitnessSigOps(tx.vin[i].scriptSig, prevout.scriptPubKey, &tx.vin[i].scriptWitness, flags);

    };
    return nSigOps;
}

bool CheckValue(CValidationState &state, CAmount nValue, CAmount &nValueOut)
{
    if (nValue < 0)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative");
    if (nValue > MAX_MONEY)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge");
    nValueOut += nValue;

    return true;
}

bool CheckStandardOutput(CValidationState &state, const Consensus::Params& consensusParams, int nTime, const CTransaction &tx, const CTxOutStandard *p, CAmount &nValueOut)
{
    if (!CheckValue(state, p->nValue, nValueOut))
        return false;

    if( p->vecSignature.size() > CPubKey::COMPACT_SIGNATURE_SIZE ){
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-signature-size");
    }

    bool fIsCoinStake = false;

    if (HasIsCoinstakeOp(p->scriptPubKey))
    {
        fIsCoinStake = true;
        if (GetAdjustedTime() < consensusParams.OpIsCoinstakeTime)
            return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-opiscoinstake");
        if (!consensusParams.fAllowOpIsCoinstakeWithP2PKH)
        {
            if (IsSpendScriptP2PKH(p->scriptPubKey))
                return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-opiscoinstake-spend-p2pkh");
        }
    }

    if( !tx.IsCoinBase() ){
        CKeyID keyId1, keyId2;
        CScript script1, script2;
        uint256 nSigHash1;
        uint256 nSigHash2;

        if( fIsCoinStake ){

            if(!SplitConditionalCoinstakeScript(*p->GetPScriptPubKey(), script1, script2)){
                return state.DoS(100, false, REJECT_INVALID, "bad-staking-out-script-coinstake");
            }

            if(!ExtractStakingKeyID(script1, keyId1)){
                return state.DoS(100, false, REJECT_INVALID, "bad-staking-out-script-coinstake-dest1");
            }

            if(!ExtractStakingKeyID(script2, keyId2)){
                return state.DoS(100, false, REJECT_INVALID, "bad-staking-out-script-coinstake-dest2");
            }

            nSigHash1 = SignatureHashStakingOutput(keyId1, p->nValue, tx.vin);
            nSigHash2 = SignatureHashStakingOutput(keyId2, p->nValue, tx.vin);

        }else if(ExtractStakingKeyID(*p->GetPScriptPubKey(), keyId1)){

            const DevFundSettings *pDevFundSettings = Params().GetDevFundSettings(nTime);
            CKeyID keyIdDev;
            if(CBitcoinAddress(pDevFundSettings->sDevFundAddresses).GetKeyID(keyIdDev) &&
               keyId1 == keyIdDev){
                return true;;
            }

            nSigHash1 = SignatureHashStakingOutput(keyId1, p->nValue, tx.vin);
        }else{
            return state.DoS(100, false, REJECT_INVALID, "bad-staking-out-script-dest");
        }

        CPubKey pubKey1, pubKey2;
        bool fP1 = pubKey1.RecoverCompact(nSigHash1, p->vecSignature);
        bool fP2 = pubKey2.RecoverCompact(nSigHash2, p->vecSignature);
        if (!fP1 && !fP2 ){
            return state.DoS(100, false, REJECT_INVALID, "bad-staking-out-signature");
        }

        if( pubKey1.GetID() != keyId1 && pubKey2.GetID() != keyId2 ){
            return state.DoS(100, false, REJECT_INVALID, "bad-staking-out-signature-key");
        }
    }

    return true;
}

bool CheckAnonOutput(CValidationState &state, const CTxOutRingCT *p)
{
/*    if (Params().NetworkID() == "main")
        return state.DoS(100, false, REJECT_INVALID, "AnonOutput in mainnet"); */

    if (p->vData.size() < 33 || p->vData.size() > 33 + 5)
        return state.DoS(100, false, REJECT_INVALID, "bad-rctout-ephem-size");

    size_t nRangeProofLen = 5134;
    if (p->vRangeproof.size() > nRangeProofLen)
        return state.DoS(100, false, REJECT_INVALID, "bad-rctout-rangeproof-size");

    if ((fBusyImporting) && fSkipRangeproof)
        return true;

    uint64_t min_value, max_value;
    int rv = secp256k1_rangeproof_verify(secp256k1_ctx_blind, &min_value, &max_value,
        &p->commitment, p->vRangeproof.data(), p->vRangeproof.size(),
        nullptr, 0,
        secp256k1_generator_h);

    if (LogAcceptCategory(BCLog::RINGCT))
        LogPrintf("%s: rv, min_value, max_value %d, %s, %s\n", __func__,
            rv, FormatMoney((CAmount)min_value), FormatMoney((CAmount)max_value));

    if (rv != 1)
        return state.DoS(100, false, REJECT_INVALID, "bad-rctout-rangeproof-verify");

    return true;
}

bool CheckDataOutput(CValidationState &state, const CTxOutData *p)
{
    if (p->vData.size() < 1)
        return state.DoS(100, false, REJECT_INVALID, "bad-output-data-size");

    const size_t MAX_DATA_OUTPUT_SIZE = 34 + 5 + 34; // DO_STEALTH 33, DO_STEALTH_PREFIX 4, DO_NARR_CRYPT (max 32 bytes)
    if (p->vData.size() > MAX_DATA_OUTPUT_SIZE)
        return state.DoS(100, false, REJECT_INVALID, "bad-output-data-size");

    return true;
}

bool CheckTransaction(const CTransaction& tx, CValidationState &state, bool fCheckDuplicateInputs)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty");

    // Size limits (this doesn't take the witness into account, as that hasn't been checked for malleability)
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-oversize");

    if (tx.IsBitcoinCVersion())
    {
        const Consensus::Params& consensusParams = Params().GetConsensus();
        if (tx.vpout.empty())
            return state.DoS(10, false, REJECT_INVALID, "bad-txns-vpout-empty");
        if (!tx.vout.empty())
            return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-not-empty");

        size_t nStandardOutputs = 0;
        CAmount nValueOut = 0;
        size_t nDataOutputs = 0;
        int nTime = GetAdjustedTime();
        for (const auto &txout : tx.vpout)
        {
            switch (txout->nVersion)
            {
                case OUTPUT_STANDARD:
                    if (!CheckStandardOutput(state, consensusParams, nTime, tx, (CTxOutStandard*) txout.get(), nValueOut))
                        return false;
                    nStandardOutputs++;
                    break;
                case OUTPUT_RINGCT:
                    if (!CheckAnonOutput(state, (CTxOutRingCT*) txout.get()))
                        return false;
                    break;
                case OUTPUT_DATA:
                    if (!CheckDataOutput(state, (CTxOutData*) txout.get()))
                        return false;
                    nDataOutputs++;
                    break;
                default:
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-unknown-output-version");
            };

            if (!MoneyRange(nValueOut))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge");
        };

        if (nDataOutputs > 1 + nStandardOutputs) // extra 1 for ct fee output
            return state.DoS(100, false, REJECT_INVALID, "too-many-data-outputs");
    } else
    {
        if (fBitcoinCMode)
            return state.DoS(100, false, REJECT_INVALID, "bad-txn-version");

        if (tx.vout.empty())
            return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-empty");

        // Check for negative or overflow output values
        CAmount nValueOut = 0;
        for (const auto& txout : tx.vout)
        {
            if (txout.nValue < 0)
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative");
            if (txout.nValue > MAX_MONEY)
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge");
            nValueOut += txout.nValue;
            if (!MoneyRange(nValueOut))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge");
        }
    };

    // Check for duplicate inputs - note that this check is slow so we skip it in CheckBlock
    if (fCheckDuplicateInputs) {
        std::set<COutPoint> vInOutPoints;
        for (const auto& txin : tx.vin)
        {
            if (!txin.IsAnonInput()
                && !vInOutPoints.insert(txin.prevout).second)
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate");
        };
    }

    if (tx.IsCoinBase())
    {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");
    } else
    {
        for (const auto& txin : tx.vin)
            if (!txin.IsAnonInput() && txin.prevout.IsNull())
                return state.DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null");
    };

    return true;
}

bool Consensus::CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, CAmount& nTxFee)
{
    // reset per tx
    state.fHasAnonOutput = false;
    state.fHasAnonInput = false;

    // early out for bitcoinc txns
    if (tx.IsBitcoinCVersion() && tx.vin.size() < 1) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txn-no-inputs", false,
                         strprintf("%s: no inputs", __func__));
    }

    // are the actual inputs available?
    if (!inputs.HaveInputs(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-missingorspent", false,
                         strprintf("%s: inputs missing/spent", __func__));
    }

    std::vector<const secp256k1_pedersen_commitment*> vpCommitsIn, vpCommitsOut;
    size_t nStandard = 0, nRingCT = 0;
    CAmount nValueIn = 0;
    CAmount nFees = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        if (tx.vin[i].IsAnonInput())
        {
            state.fHasAnonInput = true;
            nRingCT++;
            continue;
        };

        const COutPoint &prevout = tx.vin[i].prevout;
        const Coin& coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        // If prev is coinbase or coinstake, check that it's matured
        if (coin.IsCoinBase())
        {
            if (nSpendHeight - coin.nHeight < COINBASE_MATURITY)
            {
                if (fBitcoinCMode)
                {
                    // Scale in the depth restriction to start the chain
                    int nRequiredDepth = std::min(COINBASE_MATURITY, (int)(coin.nHeight / 2));
                    if (nSpendHeight - coin.nHeight < nRequiredDepth) {
                        return state.Invalid(false,
                            REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                            strprintf("tried to spend coinbase at height %d at depth %d, required %d", coin.nHeight, nSpendHeight - coin.nHeight, nRequiredDepth));
                    }
                } else
                return state.Invalid(false,
                    REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                    strprintf("tried to spend coinbase at depth %d", nSpendHeight - coin.nHeight));
            }
        }

        // Check for negative or overflow input values
        if (fBitcoinCMode)
        {
            if (coin.nType == OUTPUT_STANDARD)
            {
                nValueIn += coin.out.nValue;
                if (!MoneyRange(coin.out.nValue) || !MoneyRange(nValueIn))
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange");
                nStandard++;
            } else
            {
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-input-type");
            };
        } else
        {
            nValueIn += coin.out.nValue;
            if (!MoneyRange(coin.out.nValue) || !MoneyRange(nValueIn))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange");
        };
    }

    if ((nStandard > 0) + (nRingCT > 0) > 1)
        return state.DoS(100, false, REJECT_INVALID, "mixed-input-types");

    size_t nRingCTInputs = nRingCT;
    // GetPlainValueOut adds to nStandard, nRingCT
    CAmount nPlainValueOut = tx.GetPlainValueOut(nStandard, nRingCT);
    state.fHasAnonOutput = nRingCT > nRingCTInputs;

    nTxFee = 0;
    if (fBitcoinCMode)
    {
        if (!tx.IsCoinStake())
        {
            // Tally transaction fees
            if (nRingCT > 0)
            {
                if (!tx.GetCTFee(nTxFee))
                    return state.DoS(100, error("%s: bad-fee-output", __func__),
                        REJECT_INVALID, "bad-fee-output");
            } else
            {
                nTxFee = nValueIn - nPlainValueOut;

                if (nValueIn < nPlainValueOut)
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout", false,
                        strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn), FormatMoney(nPlainValueOut)));
            };

            if (nTxFee < 0)
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-negative");
            nFees += nTxFee;
            if (!MoneyRange(nFees))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-outofrange");

            // Enforce smsg fees
            CAmount nTotalMsgFees = 0;
            for (const auto &v : tx.vpout)
            {
                if (!v->IsType(OUTPUT_DATA))
                    continue;
                CTxOutData *txd = (CTxOutData*) v.get();
                if (txd->vData.size() < 25 || txd->vData[0] != DO_FUND_MSG)
                    continue;
                size_t n = (txd->vData.size()-1) / 24;
                for (size_t k = 0; k < n; ++k)
                {
                    uint32_t *nAmount = (uint32_t*)&txd->vData[1+k*24+20];
                    nTotalMsgFees += *nAmount;
                };
            };
            if (nTotalMsgFees > 0)
            {
                size_t nTxBytes = GetVirtualTransactionSize(tx);
                CFeeRate fundingTxnFeeRate = CFeeRate(smsg::nFundingTxnFeePerK);
                CAmount nTotalExpectedFees = nTotalMsgFees + fundingTxnFeeRate.GetFee(nTxBytes);

                if (nTxFee < nTotalExpectedFees)
                {
                    if (state.fEnforceSmsgFees)
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-smsg", false,
                            strprintf("fees (%s) < expected (%s)", FormatMoney(nTxFee), FormatMoney(nTotalExpectedFees)));
                    else
                        LogPrintf("%s: bad-txns-fee-smsg, %d expected %d, not enforcing.\n", __func__, nTxFee, nTotalExpectedFees);
                };
            };
        } else
        {
            // Return stake reward in nTxFee
            nTxFee = nPlainValueOut - nValueIn;
            if (nRingCT > 0) { // counters track both outputs and inputs
                return state.DoS(100, error("ConnectBlock(): non-standard elements in coinstake"),
                     REJECT_INVALID, "bad-coinstake-outputs");
            };
        };
    } else
    {
        if (nValueIn < tx.GetValueOut())
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout", false,
                strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn), FormatMoney(tx.GetValueOut())));

        // Tally transaction fees
        nTxFee = nValueIn - tx.GetValueOut();
        if (nTxFee < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-negative");
        nFees += nTxFee;
        if (!MoneyRange(nFees))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-outofrange");
    };

    return true;
}


uint256 SignatureHashStakingOutput(const CKeyID& keyId, const CAmount nAmount, const std::vector<CTxIn>& vin)
{
    assert(vin.size());

    CHashWriter ss(SER_GETHASH, 0);
    // Address of the convert output
    ss << keyId;

    // Amount of the convert output
    ss << nAmount;

    for( auto txin : vin ){
        ss << txin.prevout;
    }

    return ss.GetHash();
}
