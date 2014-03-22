///////////////////////////////////////////////////////////////////////////////
//
// coindb.cpp
//
// Copyright (c) 2013-2014 Eric Lombrozo
//
// All Rights Reserved.
//

#include <cli.hpp>
#include <Base58Check.h>

#include <odb/database.hxx>
#include <odb/transaction.hxx>

#include <Vault.h>
#include <Schema-odb.hxx>

#include <random.h>

#include <iostream>
#include <sstream>

using namespace std;
using namespace odb::core;
using namespace CoinDB;

// Vault operations
cli::result_t cmd_create(bool bHelp, const cli::params_t& params)
{
    if (bHelp || params.size() != 1)
        return "create <filename> - create a new vault.";

    Vault vault(params[0], true);

    stringstream ss;
    ss << "Vault " << params[0] << " created.";
    return ss.str();
}

// Keychain operations
cli::result_t cmd_keychainexists(bool bHelp, const cli::params_t& params)
{
    if (bHelp || params.size() != 2)
        return "keychainexists <filename> <keychain_name> - check if a keychain exists.";

    Vault vault(params[0], false);
    bool bExists = vault.keychainExists(params[1]);

    stringstream ss;
    ss << (bExists ? "true" : "false");
    return ss.str();
}

cli::result_t cmd_newkeychain(bool bHelp, const cli::params_t& params)
{
    if (bHelp || params.size() != 2)
        return "newkeychain <filename> <keychain_name> - create a new keychain.";

    Vault vault(params[0], false);
    vault.newKeychain(params[1], random_bytes(32));

    stringstream ss;
    ss << "Added keychain " << params[1] << " to vault " << params[0] << ".";
    return ss.str();
}
/*
cli::result_t cmd_erasekeychain(bool bHelp, const cli::params_t& params)
{
    if (bHelp || params.size() != 2) {
        return "erasekeychain <filename> <keychain_name> - erase a keychain.";
    }

    Vault vault(params[0], false);
    if (!vault.keychainExists(params[1]))
        throw runtime_error("Keychain not found.");

    vault.eraseKeychain(params[1]);

    stringstream ss;
    ss << "Keychain " << params[1] << " erased.";
    return ss.str();
}
*/
cli::result_t cmd_renamekeychain(bool bHelp, const cli::params_t& params)
{
    if (bHelp || params.size() != 3)
        return "renamekeychain <filename> <oldname> <newname> - rename a keychain.";

    Vault vault(params[0], false);
    vault.renameKeychain(params[1], params[2]);

    stringstream ss;
    ss << "Keychain " << params[1] << " renamed to " << params[2] << ".";
    return ss.str();
}

cli::result_t cmd_keychaininfo(bool bHelp, const cli::params_t& params)
{
    if (bHelp || params.size() != 2)
        return "keychaininfo <filename> <keychain_name> - display keychain information.";

    Vault vault(params[0], false);
    shared_ptr<Keychain> keychain = vault.getKeychain(params[1]);

    stringstream ss;
    ss << "id:        " << keychain->id() << endl
       << "name:      " << keychain->name() << endl
       << "depth:     " << keychain->depth() << endl
       << "parent_fp: " << keychain->parent_fp() << endl
       << "child_num: " << keychain->child_num() << endl
       << "pubkey:    " << uchar_vector(keychain->pubkey()).getHex() << endl
       << "hash:      " << uchar_vector(keychain->hash()).getHex();
    return ss.str();
}

/*
cli::result_t cmd_listkeychains(bool bHelp, const cli::params_t& params)
{
    if (bHelp || params.size() != 1) {
        return "listkeychains <filename> - list all keychains in vault.";
    }

    stringstream ss;

    Vault vault(params[0], false);
    std::vector<KeychainInfo> keychains = vault.getKeychains();
    bool first = true;
    for (auto& keychain: keychains) {
        if (first) {
            first = false;
        }
        else {
            ss << endl;
        }
        ss << "id: " << keychain.id() << " name: " << keychain.name() << " hash: " << uchar_vector(keychain.hash()).getHex() << " numkeys: " << keychain.numkeys();
    }

    return ss.str();
}
*/

// Account operations
cli::result_t cmd_accountexists(bool bHelp, const cli::params_t& params)
{
    if (bHelp || params.size() != 2)
        return "accountexists <filename> <account_name> - check if an account exists.";

    Vault vault(params[0], false);
    bool bExists = vault.accountExists(params[1]);

    stringstream ss;
    ss << (bExists ? "true" : "false");
    return ss.str();
}

cli::result_t cmd_newaccount(bool bHelp, const cli::params_t& params)
{
    if (bHelp || params.size() < 4)
        return "newaccount <filename> <account_name> <minsigs> <keychain1> [keychain2] [keychain3] ... - create a new account using specified keychains.";

    uint32_t minsigs = strtoull(params[2].c_str(), NULL, 10);
    std::vector<std::string> keychain_names;
    for (size_t i = 3; i < params.size(); i++)
        keychain_names.push_back(params[i]);

    Vault vault(params[0], false);
    vault.newAccount(params[1], minsigs, keychain_names);

    stringstream ss;
    ss << "Added account " << params[1] << " to vault " << params[0] << ".";
    return ss.str();
}

cli::result_t cmd_renameaccount(bool bHelp, const cli::params_t& params)
{
    if (bHelp || params.size() != 3)
        return "renameaccount <filename> <old_account_name> <new_account_name> - rename an account.";

    Vault vault(params[0], false);
    vault.renameAccount(params[1], params[2]);

    stringstream ss;
    ss << "Renamed account " << params[1] << " to " << params[2] << ".";
    return ss.str();
}

cli::result_t cmd_accountinfo(bool bHelp, const cli::params_t& params)
{
    if (bHelp || params.size() != 2)
        return "accountinfo <filename> <account_name> - display account info.";

    Vault vault(params[0], false);
    AccountInfo accountInfo = vault.getAccountInfo(params[1]);

    // TODO: add the following list generating routine to a general utils library
    bool addComma = false;
    std::string keychain_list;
    for (auto& keychain_name: accountInfo.keychain_names())
    {
        if (addComma)   keychain_list += ", ";
        else            addComma = true;

        keychain_list += keychain_name;
    }

    addComma = false;
    std::string bin_list;
    for (auto& bin_name: accountInfo.bin_names())
    {
        if (addComma)   bin_list += ", ";
        else            addComma = true;

        bin_list += bin_name;
    }

    stringstream ss;
    ss << "id:               " << accountInfo.id() << endl
       << "name:             " << accountInfo.name() << endl
       << "minsigs:          " << accountInfo.minsigs() << endl
       << "keychains:        " << keychain_list << endl
       << "unused_pool_size: " << accountInfo.unused_pool_size() << endl
       << "time_created:     " << accountInfo.time_created() << endl
       << "bins:             " << bin_list;
    return ss.str();
}

cli::result_t cmd_newaccountbin(bool bHelp, const cli::params_t& params)
{
    if (bHelp || params.size() != 3)
        return "newaccountbin <filename> <account_name> <bin_name> - add new account bin.";

    Vault vault(params[0], false);
    vault.addAccountBin(params[1], params[2]);

    stringstream ss;
    ss << "Account bin " << params[2] << " added to account " << params[1] << ".";
    return ss.str(); 
}

// TODO: Get from config file
const unsigned char PAY_TO_SCRIPT_HASH_VERSION = 0x05;

cli::result_t cmd_newtxout(bool bHelp, const cli::params_t& params)
{
    if (bHelp || params.size() < 2 || params.size() > 5)
        return "newtxout <filename> <account_name> [bin_name = @default] [value = 0] [label = ""] - get a new payment txout.";

    Vault vault(params[0], false);
    std::string bin_name = params.size() > 2 ? params[2] : std::string("@default");
    uint64_t value = params.size() > 3 ? strtoull(params[3].c_str(), NULL, 0) : 0;
    std::string label = params.size() > 4 ? params[4] : std::string("");
    std::shared_ptr<TxOut> txout = vault.newTxOut(params[1], label, value, bin_name);

    using namespace CoinQ::Script;
    std::string address;
    payee_t payee = getScriptPubKeyPayee(txout->script());
    if (payee.first == SCRIPT_PUBKEY_PAY_TO_SCRIPT_HASH)
        address = toBase58Check(payee.second, PAY_TO_SCRIPT_HASH_VERSION);
    else
        address = "N/A";

    stringstream ss;
    ss << "account:     " << params[1] << endl
       << "account bin: " << bin_name << endl
       << "value:       " << value << endl
       << "label:       " << label << endl
       << "script:      " << uchar_vector(txout->script()).getHex() << endl
       << "address:     " << address;
    return ss.str(); 
}

int main(int argc, char* argv[])
{
    cli::command_map cmds("CoinDB by Eric Lombrozo v0.2.0");

    // Vault operations
    cmds.add("create", &cmd_create);

    // Keychain operations
    cmds.add("keychainexists", &cmd_keychainexists);
    cmds.add("newkeychain", &cmd_newkeychain);
    //cmds.add("erasekeychain", &cmd_erasekeychain);
    cmds.add("renamekeychain", &cmd_renamekeychain);
    cmds.add("keychaininfo", &cmd_keychaininfo);

    // Account operations
    cmds.add("accountexists", &cmd_accountexists);
    cmds.add("newaccount", &cmd_newaccount);
    cmds.add("renameaccount", &cmd_renameaccount);
    cmds.add("accountinfo", &cmd_accountinfo);
    cmds.add("newaccountbin", &cmd_newaccountbin);
    cmds.add("newtxout", &cmd_newtxout);

    try 
    {
        return cmds.exec(argc, argv);
    }
    catch (const std::exception& e)
    {
        cerr << e.what() << endl;
        return 1;
    }
    return 0;
}

