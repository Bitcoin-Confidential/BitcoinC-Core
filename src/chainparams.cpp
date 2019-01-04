// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
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
};

int64_t CChainParams::GetProofOfStakeReward(const CBlockIndex *pindexPrev, int64_t nFees) const
{
    int64_t nSubsidy;

    nSubsidy = (pindexPrev->nMoneySupply / COIN) * GetCoinYearReward(pindexPrev->nTime) / (365 * 24 * (60 * 60 / nTargetSpacing));

    if (LogAcceptCategory(BCLog::POS) && gArgs.GetBoolArg("-printcreation", false))
        LogPrintf("GetProofOfStakeReward(): create=%s\n", FormatMoney(nSubsidy).c_str());

    return nSubsidy + nFees;
};

bool CChainParams::CheckImportCoinbase(int nHeight, uint256 &hash) const
{
    for (auto &cth : Params().vImportedCoinbaseTxns)
    {
        if (cth.nHeight != (uint32_t)nHeight)
            continue;

        if (hash == cth.hash)
            return true;
        return error("%s - Hash mismatch at height %d: %s, expect %s.", __func__, nHeight, hash.ToString(), cth.hash.ToString());
    };

    return error("%s - Unknown height.", __func__);
};


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

const std::pair<const char*, CAmount> regTestOutputs[] = {
/*    std::make_pair("585c2b3914d9ee51f8e710304e386531c3abcc82", 10000 * COIN),
    std::make_pair("c33f3603ce7c46b423536f0434155dad8ee2aa1f", 10000 * COIN),
    std::make_pair("72d83540ed1dcf28bfaca3fa2ed77100c2808825", 10000 * COIN),
    std::make_pair("69e4cc4c219d8971a253cd5db69a0c99c4a5659d", 10000 * COIN),
    std::make_pair("eab5ed88d97e50c87615a015771e220ab0a0991a", 10000 * COIN),
    std::make_pair("119668a93761a34a4ba1c065794b26733975904f", 10000 * COIN),
    std::make_pair("6da49762a4402d199d41d5778fcb69de19abbe9f", 10000 * COIN),
    std::make_pair("27974d10ff5ba65052be7461d89ef2185acbe411", 10000 * COIN),
    std::make_pair("89ea3129b8dbf1238b20a50211d50d462a988f61", 10000 * COIN),
    std::make_pair("3baab5b42a409b7c6848a95dfd06ff792511d561", 10000 * COIN),

    std::make_pair("649b801848cc0c32993fb39927654969a5af27b0", 5000 * COIN),
    std::make_pair("d669de30fa30c3e64a0303cb13df12391a2f7256", 5000 * COIN),
    std::make_pair("f0c0e3ebe4a1334ed6a5e9c1e069ef425c529934", 5000 * COIN),
    std::make_pair("27189afe71ca423856de5f17538a069f22385422", 5000 * COIN),
    std::make_pair("0e7f6fe0c4a5a6a9bfd18f7effdd5898b1f40b80", 5000 * COIN),
*/};
const size_t nGenesisOutputsRegtest = sizeof(regTestOutputs) / sizeof(regTestOutputs[0]);

const std::pair<const char*, CAmount> genesisOutputs[] = {
    std::make_pair("4A6763806860D43D0B1376D012D3EA1E89CF97A3",7017084118),
    std::make_pair("E359427F1BC5B29A8A092A387D19EC123F2C35F1",221897417980),
/*    std::make_pair("711b5e1fd0b0f4cdf92cb53b00061ef742dda4fb",120499999),
    std::make_pair("20c17c53337d80408e0b488b5af7781320a0a311",18074999),
    std::make_pair("aba8c6f8dbcf4ecfb598e3c08e12321d884bfe0b",92637054909),
    std::make_pair("1f3277a84a18f822171d720f0132f698bcc370ca",3100771006662),
    std::make_pair("8fff14bea695ffa6c8754a3e7d518f8c53c3979a",465115650998),
    std::make_pair("e54967b4067d91a777587c9f54ee36dd9f1947c4",669097504996),
    std::make_pair("7744d2ac08f2e1d108b215935215a4e66d0262d2",802917005996),
    std::make_pair("a55a17e86246ea21cb883c12c709476a09b4885c",267639001997),
    std::make_pair("4e00dce8ab44fd4cafa34839edf8f68ba7839881",267639001997),
    std::make_pair("702cae5d2537bfdd5673ac986f910d6adb23510a",254257051898),
    std::make_pair("b19e494b0033c5608a7d153e57d7fdf3dfb51bb7",1204260290404),
    std::make_pair("6909b0f1c94ea1979ed76e10a5a49ec795a8f498",1204270995964),
    std::make_pair("05a06af3b29dade9f304244d934381ac495646c1",236896901156),
    std::make_pair("557e2b3205719931e22853b27920d2ebd6147531",155127107700),
    std::make_pair("ad16fb301bd21c60c5cb580b322aa2c61b6c5df2",115374999),
    std::make_pair("182c5cfb9d17aa8d8ff78940135ca8d822022f32",17306249),
    std::make_pair("b8a374a75f6d44a0bd1bf052da014efe564ae412",133819500998),
    std::make_pair("fadee7e2878172dad55068c8696621b1788dccb3",133713917412),
    std::make_pair("eacc4b108c28ed73b111ff149909aacffd2cdf78",173382671567),
    std::make_pair("dd87cc0b8e0fc119061f33f161104ce691d23657",245040727620),
    std::make_pair("1c8b0435eda1d489e9f0a16d3b9d65182f885377",200226012806),
    std::make_pair("15a724f2bc643041cb35c9475cd67b897d62ca52",436119839355),
    std::make_pair("626f86e9033026be7afbb2b9dbe4972ef4b3e085",156118097804),
    std::make_pair("a4a73d99269639541cb7e845a4c6ef3e3911fcd6",108968353176),
    std::make_pair("27929b31f11471aa4b77ca74bb66409ff76d24a2",126271503135),
    std::make_pair("2d6248888c7f72cc88e4883e4afd1025c43a7f0e",35102718156),
    std::make_pair("25d8debc253f5c3f70010f41c53348ed156e7baa",80306152234),
*/};
const size_t nGenesisOutputs = sizeof(genesisOutputs) / sizeof(genesisOutputs[0]);

const std::pair<const char*, CAmount> genesisOutputsTestnet[] = {
/*    std::make_pair("46a064688dc7beb5f70ef83569a0f15c7abf4f28",7017084118),
    std::make_pair("9c97b561ac186bd3758bf690036296d36b1fd019",221897417980),
    std::make_pair("118a92e28242a73244fb03c96b7e1429c06f979f",120499999),
    std::make_pair("cae4bf990ce39624e2f77c140c543d4b15428ce7",18074999),
    std::make_pair("9d6b7b5874afc100eb82a4883441a73b99d9c306",92637054909),
    std::make_pair("f989e2deedb1f09ed10310fc0d7da7ebfb573326",3100771006662),
    std::make_pair("4688d6701fb4ae2893d3ec806e6af966faf67545",465115650998),
    std::make_pair("40e07b038941fb2616a54a498f763abae6d4f280",669097504996),
    std::make_pair("c43f7c57448805a068a440cc51f67379ca946264",802917005996),
    std::make_pair("98b7269dbf0c2e3344fb41cd60e75db16d6743a6",267639001997),
    std::make_pair("85dceec8cdbb9e24fe07af783e4d273d1ae39f75",267639001997),
    std::make_pair("ddc05d332b7d1a18a55509f34c786ccb65bbffbc",245040727620),
    std::make_pair("8b04d0b2b582c986975414a01cb6295f1c33d0e9",1204260290404),
    std::make_pair("1e9ff4c3ac6d0372963e92a13f1e47409eb62d37",1204270995964),
    std::make_pair("687e7cf063cd106c6098f002fa1ea91d8aee302a",236896901156),
    std::make_pair("dc0be0edcadd4cc97872db40bb8c2db2cebafd1c",155127107700),
    std::make_pair("21efcbfe37045648180ac68b406794bde77f9983",115374999),
    std::make_pair("deaf53dbfbc799eed1171269e84c733dec22f517",17306249),
    std::make_pair("200a0f9dba25e00ea84a4a3a43a7ea6983719d71",133819500998),
    std::make_pair("2d072fb1a9d1f7dd8df0443e37e9f942eab58680",133713917412),
    std::make_pair("0850f3b7caf3b822bb41b9619f8edf9b277402d0",173382671567),
    std::make_pair("ec62fbd782bf6f48e52eea75a3c68a4c3ab824c0",254257051898),
    std::make_pair("c6dcb0065e98f5edda771c594265d61e38cf63a0",200226012806),
    std::make_pair("e5f9a711ccd7cb0d2a70f9710229d0d0d7ef3bda",436119839355),
    std::make_pair("cae1527d24a91470aeb796f9d024630f301752ef",156118097804),
    std::make_pair("604f36860d79a9d72b827c99409118bfe16711bd",108968353176),
    std::make_pair("f02e5891cef35c9c5d9a770756b240aba5ba3639",126271503135),
    std::make_pair("8251b4983be1027a17dc3b977502086f08ba8910",35102718156),
    std::make_pair("b991d98acde28455ecb0193fefab06841187c4e7",80306152234),
*/};
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

/*
    // Foundation Fund Raiser Funds
    // rVDQRVBKnQEfNmykMSY9DHgqv8s7XZSf5R fc118af69f63d426f61c6a4bf38b56bcdaf8d069
    OUTPUT_PTR<CTxOutStandard> out = MAKE_OUTPUT<CTxOutStandard>();
    out->nValue = 397364 * COIN;
    out->scriptPubKey = CScript() << OP_HASH160 << ParseHex("fc118af69f63d426f61c6a4bf38b56bcdaf8d069") << OP_EQUAL;
    txNew.vpout.push_back(out);

    // rVDQRVBKnQEfNmykMSY9DHgqv8s7XZSf5R fc118af69f63d426f61c6a4bf38b56bcdaf8d069
    out = MAKE_OUTPUT<CTxOutStandard>();
    out->nValue = 296138 * COIN;
    out->scriptPubKey = CScript() << OP_HASH160 << ParseHex("89ca93e03119d53fd9ad1e65ce22b6f8791f8a49") << OP_EQUAL;
    txNew.vpout.push_back(out);

    // Community Initative
    // rAybJ7dx4t6heHy99WqGcXkoT4Bh3V9qZ8 340288104577fcc3a6a84b98f7eac1a54e5287ee
    out = MAKE_OUTPUT<CTxOutStandard>();
    out->nValue = 156675 * COIN;
    out->scriptPubKey = CScript() << OP_HASH160 << ParseHex("89ca93e03119d53fd9ad1e65ce22b6f8791f8a49") << OP_EQUAL;
    txNew.vpout.push_back(out);

    // Contributors Left Over Funds
    // rAvmLShYFZ78aAHhFfUFsrHMoBuPPyckm5 3379aa2a4379ae6c51c7777d72e8e0ffff71881b
    out = MAKE_OUTPUT<CTxOutStandard>();
    out->nValue = 216346 * COIN;
    out->scriptPubKey = CScript() << OP_HASH160 << ParseHex("89ca93e03119d53fd9ad1e65ce22b6f8791f8a49") << OP_EQUAL;
    txNew.vpout.push_back(out);

    // Reserved BitcoinC for primary round
    // rLWLm1Hp7im3mq44Y1DgyirYgwvrmRASib 9c8c6c8c698f074180ecfdb38e8265c11f2a62cf
    out = MAKE_OUTPUT<CTxOutStandard>();
    out->nValue = 996000 * COIN;
    out->scriptPubKey = CScript() << 1512000000 << OP_CHECKLOCKTIMEVERIFY << OP_DROP << OP_HASH160<< ParseHex("9c8c6c8c698f074180ecfdb38e8265c11f2a62cf") << OP_EQUAL; // 2017-11-30
    txNew.vpout.push_back(out);
*/

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

/*
//    txNew.vpout.resize(nGenesisOutputs);/*
    for (size_t k = 0; k < nGenesisOutputs; ++k)
    {
        OUTPUT_PTR<CTxOutStandard> out = MAKE_OUTPUT<CTxOutStandard>();
        out->nValue = genesisOutputs[k].second;
        out->scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex(genesisOutputs[k].first) << OP_EQUALVERIFY << OP_CHECKSIG;
        txNew.vpout[k] = out;
    };
*/
    // Development Address
    // bH54zbnXUHUhcvyP8fTboMVUXoPfTAjU6E
    OUTPUT_PTR<CTxOutStandard> out = MAKE_OUTPUT<CTxOutStandard>();
    out->nValue = 7367509500 * COIN;
    out->scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("2f9f0e260ed2e09956109d62603312968a19642f") << OP_EQUALVERIFY << OP_CHECKSIG;
    txNew.vpout.push_back(out);

/*
    out = MAKE_OUTPUT<CTxOutStandard>();
    out->nValue = 296138 * COIN;
    out->scriptPubKey = CScript() << OP_HASH160 << ParseHex("5766354dcb13caff682ed9451b9fe5bbb786996c") << OP_EQUAL;
    txNew.vpout.push_back(out);

    // Community Initative
    // RKKgSiQcMjbC8TABRoyyny1gTU4fAEiQz9
    out = MAKE_OUTPUT<CTxOutStandard>();
    out->nValue = 156675 * COIN;
    out->scriptPubKey = CScript() << OP_HASH160 << ParseHex("6e29c4a11fd54916d024af16ca913cdf8f89cb31") << OP_EQUAL;
    txNew.vpout.push_back(out);

    // Contributors Left Over Funds
    // RKiaVeyLUp7EmwHtCP92j8Vc1AodhpWi2U
    out = MAKE_OUTPUT<CTxOutStandard>();
    out->nValue = 216346 * COIN;
    out->scriptPubKey = CScript() << OP_HASH160 << ParseHex("727e5e75929bbf26912dd7833971d77e7450a33e") << OP_EQUAL;
    txNew.vpout.push_back(out);

    // Reserved BitcoinC for primary round
    // RNnoeeqBTkpPQH8d29Gf45dszVj9RtbmCu
    out = MAKE_OUTPUT<CTxOutStandard>();
    out->nValue = 996000 * COIN;
    out->scriptPubKey = CScript() << 1512000000 << OP_CHECKLOCKTIMEVERIFY << OP_DROP << OP_HASH160<< ParseHex("9433643b4fd5de3ebd7fdd68675f978f34585af1") << OP_EQUAL; // 2017-11-30
    txNew.vpout.push_back(out);
*/

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
//        consensus.powLimit = uint256S("000000000000bfffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.powLimit = uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
//        consensus.powLimit = uint256S("000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
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
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000000000010001");

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

        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlockMainNet(1544595191, 38335, 0x1f00ffff); // 2018-12-12
        consensus.hashGenesisBlock = genesis.GetHash();
/*
 //added-->
        bool fNegative;
        bool fOverflow;
        arith_uint256 bnTarget;

        bnTarget.SetCompact(0x1f00ffff, &fNegative, &fOverflow);
              for(int count = 1; count < 100000000000; count++){
                 genesis = CreateGenesisBlockMainNet(1544595191, count, 0x1f00ffff);
// default
                 if (UintToArith256(genesis.GetHash()) <= bnTarget){
                    printf("genesis.nNonce = %d\n", count);
                    printf("new mainnet genesis merkle root: %s\n", genesis.hashMerkleRoot.ToString().c_str());
                    printf("new mainnet genesis Witness merkle root: %s\n", genesis.hashWitnessMerkleRoot.ToString().c_str());
                    printf("new mainnet genesis hash: %s\n", genesis.GetHash().ToString().c_str());
                    break;
                }
                 if( count % 1000000 == 0 )
                  printf("count = %d\n", count);
		 }
///<--added*/


        assert(consensus.hashGenesisBlock == uint256S("0x000027aa4bd85c64d0a9c2d2efe747573079a2f69ed8c176ffb43b0ee8334412"));
        assert(genesis.hashMerkleRoot == uint256S("0xc7c73b255f7a9e4f2db9124e7dc11a2b10557bcd039d5e5f598f95e2f3508c14"));
        assert(genesis.hashWitnessMerkleRoot == uint256S("0x99cf3e1a63eadadf9002a8c8bcf03c04467ce0b11cd66154323320dbb515520c"));

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        vSeeds.emplace_back("mainnet-seed.bitcoinconfidential.cc/");
        vSeeds.emplace_back("dnsseed-mainnet.bitcoinconfidential.cc/");
        vSeeds.emplace_back("mainnet.bitcoinconfidential.cc/");


/*        vDevFundSettings.emplace_back(0,
           DevFundSettings("RJAPhgckEgRGVPZa9WoGSWW24spskSfLTQ", 10, 60));
        vDevFundSettings.emplace_back(consensus.OpIsCoinstakeTime,
            DevFundSettings("RBiiQBnQsVPPQkUaJVQTjsZM9K2xMKozST", 10, 60));
*/

        base58Prefixes[PUBKEY_ADDRESS]     = {0x55}; // b
        base58Prefixes[SCRIPT_ADDRESS]     = {0x0A}; // 5
        base58Prefixes[PUBKEY_ADDRESS_256] = {0x39};
        base58Prefixes[SCRIPT_ADDRESS_256] = {0x3d};
        base58Prefixes[SECRET_KEY]         = {0x1c}; // C
//        base58Prefixes[EXT_PUBLIC_KEY]     = {0x69, 0x6e, 0x82, 0xd1}; // PPAR
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x02, 0xD4, 0x13, 0xFF}; // PPAR   bpub
//         base58Prefixes[EXT_SECRET_KEY]     = {0x8f, 0x1d, 0xae, 0xb8}; // XPAR
        base58Prefixes[EXT_SECRET_KEY]     = {0x82, 0xE8, 0xDA, 0x58}; // cprv

        base58Prefixes[STEALTH_ADDRESS]    = {0xB2}; // B
        base58Prefixes[EXT_KEY_HASH]       = {0x4b}; // X
        base58Prefixes[EXT_ACC_HASH]       = {0x17}; // A
        base58Prefixes[EXT_PUBLIC_KEY_BTC] = {0x04, 0x88, 0xB2, 0x1E}; // xpub
        base58Prefixes[EXT_SECRET_KEY_BTC] = {0x04, 0x88, 0xAD, 0xE4}; // xprv

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

  //      chainTxData = ChainTxData {
            // Data from rpc: getchaintxstats 4096 7cc035d7888ee6d824cec8ff01a6287a71873d874f72a5fd3706d227b88f8e99
 //           /* nTime    */ 1544595191,
 //           /* nTxCount */ 342225,
 //           /* dTxRate  */ 0.008
 //       };

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
        consensus.fAllowOpIsCoinstakeWithP2PKH = true; // TODO: clear for next testnet
        consensus.nPaidSmsgTime = 0;
        consensus.csp2shHeight = 0x7FFFFFFF;

        consensus.powLimit = uint256S("000000000005ffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
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
//        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        //consensus.defaultAssumeValid = uint256S("0x000000000871ee6842d3648317ccc8a435eb8cc3c2429aee94faff9ba26b05a0"); //1043841

        consensus.nMinRCTOutputDepth = 12;

        pchMessageStart[0] = 0xbc;
        pchMessageStart[1] = 0x15;
        pchMessageStart[2] = 0xab;
        pchMessageStart[3] = 0x1e;
        nDefaultPort = 19789;
        nBIP44ID = 0x80000001;

        nModifierInterval = 10 * 60;    // 10 minutes
        nStakeMinConfirmations = 225;   // 225 * 2 minutes
        nTargetSpacing = 120;           // 2 minutes
        nTargetTimespan = 24 * 60;      // 24 mins


        AddImportHashesTest(vImportedCoinbaseTxns);
        SetLastImportHeight();

        nPruneAfterHeight = 1000;


        genesis = CreateGenesisBlockTestNet(1544595191, 7151182, 0x1d00ffff);
        consensus.hashGenesisBlock = genesis.GetHash();
/*
// added
               genesis = CreateGenesisBlockTestNet(1544595191, 90000, 0x1f00ffff);
               int count;
               for(count = 90000; count < 1000000000; count++){
                  genesis = CreateGenesisBlockTestNet(1544595191, count, 0x1f00ffff);
                if (UintToArith256(genesis.GetHash()) < arith_uint256("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff")){
                     printf("genesis.nNonce = %d\n", count);
                     printf("new mainnet genesis merkle root: %s\n", genesis.hashMerkleRoot.ToString().c_str());
                     printf("new mainnet genesis Witness merkle root: %s\n", genesis.hashWitnessMerkleRoot.ToString().c_str());
                     printf("new mainnet genesis hash: %s\n", genesis.GetHash().ToString().c_str());
                     assert(genesis.GetHash() == uint256S(""));
                  }
               }
*/

//        assert(consensus.hashGenesisBlock == uint256S("0x000000eca076b42f3720d246b76c66f9d3a3720126b526782f8d3ac3c40ddee1"));
//        assert(genesis.hashMerkleRoot == uint256S("0xa3af27e47506ea813815589d7a0ac4129a08efa85d81cf69502f6b7fceb4097e"));
//        assert(genesis.hashWitnessMerkleRoot == uint256S("0x6d2f8bf12c8f288d4212c24294d214326dfa9ace92590fc965e44687521266e7"));

//        assert(consensus.hashGenesisBlock == uint256S("0x0000594ada5310b367443ee0afd4fa3d0bbd5850ea4e33cdc7d6a904a7ec7c90"));
//        assert(genesis.hashMerkleRoot == uint256S("0xdf6d7bb1cadb2c430a091dfe7f45c849f94a32b586a51fc801ad1bbd06c591ec"));
//        assert(genesis.hashWitnessMerkleRoot == uint256S("0xf9e2235c9531d5a19263ece36e82c4d5b71910d73cd0b677b81c5e50d17b6cda"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        vSeeds.emplace_back("testnet-seed.bitcoinconfidential.cc/");
        vSeeds.emplace_back("dnsseed-testnet.bitcoinconfidential.cc/");

//        vDevFundSettings.push_back(std::make_pair(0, DevFundSettings("rTvv9vsbu269mjYYEecPYinDG8Bt7D86qD", 10, 60)));

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
/*                {127620, uint256S("0xe5ab909fc029b253bad300ccf859eb509e03897e7853e8bfdde2710dbf248dd1")},
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
        consensus.nPowTargetSpacing = 10 * 60;
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

        nPruneAfterHeight = 1000;


        genesis = CreateGenesisBlockRegTest(1544595191, 10468, 0x207fffff);
        consensus.hashGenesisBlock = genesis.GetHash();
/* // added
                genesis = CreateGenesisBlockRegTest(1544595191, 10000, 0x207fffff);
                int count;
                for(count = 10000; count < 1000000000; count++){
                   genesis = CreateGenesisBlockRegTest(1544595191, count, 0x207fffff);
                 if (UintToArith256(genesis.GetHash()) < arith_uint256("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff")){
                      printf("genesis.nNonce = %d\n", count);
                      printf("new mainnet genesis merkle root: %s\n", genesis.hashMerkleRoot.ToString().c_str());
                      printf("new mainnet genesis Witness merkle root: %s\n", genesis.hashWitnessMerkleRoot.ToString().c_str());
                      printf("new mainnet genesis hash: %s\n", genesis.GetHash().ToString().c_str());
                      assert(genesis.GetHash() == uint256S(""));
                   }
                }

*/
//        assert(consensus.hashGenesisBlock == uint256S("0x008d0dd5681a6d1cd8b1da50b0305dcf2d82abb350dc171d75b6af954359c338"));
        assert(genesis.hashMerkleRoot == uint256S("0xe2c6f30c2cc6ea4d516f889a03a5a0468ba567218fa13a09ec18836d35fc83e0"));
        assert(genesis.hashWitnessMerkleRoot == uint256S("0xd833c75ac31f70bc526f6da2ec443825e04a282f5bded429b2e042bd8f08fd66"));
//        assert(consensus.hashGenesisBlock == uint256S("0x6cd174536c0ada5bfa3b8fde16b98ae508fff6586f2ee24cf86686709$
//        assert(genesis.hashMerkleRoot == uint256S("0xf89653c7208af2c76a3070d436229fb782acbd065bd5810307995b9982423$
//        assert(genesis.hashWitnessMerkleRoot == uint256S("0x36b66a1aff91f34ab794da710d007777ef5e612a320e1979ac96e5$


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
        /*
        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        */

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

