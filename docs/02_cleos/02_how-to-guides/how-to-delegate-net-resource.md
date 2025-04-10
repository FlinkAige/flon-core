## Goal

Delegate network bandwidth for an account or application.

## Before you begin

Make sure you meet the following requirements:

* Familiarize with the [`fucli system delegatebw`](../03_command-reference/system/system-delegatebw.md) command and its parameters.
* Install the currently supported version of `fucli`.

[[info | Note]]
| `fucli` is bundled with the FullOn software. [Installing FullOn](../../00_install/index.md) will also install `fucli`.

* Ensure the reference system contracts from [`reference-contracts`](https://github.com/fullon-labs/reference-contracts) repository is deployed and used to manage system resources.
* Understand what an [account](/glossary.md#account) is and its role in the blockchain.
* Understand [NET bandwidth](/glossary.md#net) in an FullOn blockchain.
* Understand [CPU bandwidth](/glossary.md#cpu) in an FullOn blockchain.

## Steps

Perform the step below:

Delegate CPU bandwidth from a source account to a receiver account:

```sh
fucli system delegatebw <from> <receiver> <stake_net_quantity> <stake_cpu_quantity>
```

Where `from` is the account to delegate bandwidth from, `receiver` is the account to receive the delegated bandwidth, and `stake_net_quantity` and/or `stake_cpu_quantity` is the amount of tokens to stake for network (NET) bandwidth and/or CPU bandwidth, respectively.

Some examples are provided below:

* Delegate 0.01 SYS network bandwidth from `bob` to `alice`:

**Example Output**

```sh
fucli system delegatebw bob alice "0.01 SYS" "0 SYS"
```
```json
executed transaction: 5487afafd67bf459a20fcc2dbc5d0c2f0d1f10e33123eaaa07088046fd18e3ae  192 bytes  503 us
#         eosio <= eosio::delegatebw            {"from":"bob","receiver":"alice","stake_net_quantity":"0.0100 SYS","stake_cpu_quantity":"0.0000 SYS"...
#   eosio.token <= eosio.token::transfer        {"from":"bob","to":"eosio.stake","quantity":"0.0010 SYS","memo":"stake bandwidth"}
#  bob <= eosio.token::transfer        {"from":"bob","to":"eosio.stake","quantity":"0.0010 SYS","memo":"stake bandwidth"}
#   eosio.stake <= eosio.token::transfer        {"from":"bob","to":"eosio.stake","quantity":"0.0010 SYS","memo":"stake bandwidth"}
```
