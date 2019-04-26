// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2019 The Bitcoin Confidential Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOINC_WALLET_TEST_HDWALLET_TEST_FIXTURE_H
#define BITCOINC_WALLET_TEST_HDWALLET_TEST_FIXTURE_H

#include <test/test_bitcoin.h>

class CHDWallet;

/** Testing setup and teardown for wallet.
 */
struct HDWalletTestingSetup: public TestingSetup {
    HDWalletTestingSetup(const std::string& chainName = CBaseChainParams::MAIN);
    ~HDWalletTestingSetup();

    std::shared_ptr<CHDWallet> pwalletMain;
};

std::string StripQuotes(std::string s);

#endif // BITCOINC_WALLET_TEST_HDWALLET_TEST_FIXTURE_H

