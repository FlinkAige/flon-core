## Goal

Query infomation about a block.

## Before you begin

Make sure to meet the following requirements:

* Familiarize with the [`fucli get block`](../03_command-reference/get/block.md) command and its parameters.
* Install the currently supported version of `fucli`.

[[info | Note]]
| `fucli` is bundled with the FullOn software. [Installing FullOn](../../00_install/index.md) will also install `fucli`.

* Understand what a [block](/glossary.md#block) is and its role in the blockchain.
* Understand the [block lifecycle](/protocol-guides/01_consensus_protocol.md#5-block-lifecycle) in the FullOn consensus protocol.

## Steps

Perform the step below:

Retrieve full information about a block:

```sh
fucli get block <block_number_or_id>
```

Where `block_number_or_id` is the specified block number or block ID.

Some examples are provided below:

* Query the testnet to retrieve full block information about block number `48351112` or block ID `02e1c7888a92206573ae38d00e09366c7ba7bc54cd8b7996506f7d2a619c43ba`:

**Example Output**

```sh
fucli -u https://choiceofyourprovider get block 48351112
```
Reference to [testnet providers](/resources/index.md)

```json
{
  "timestamp": "2021-01-28T17:58:59.500",
  "producer": "inith",
  "confirmed": 0,
  "previous": "02e1c78787ff4d4ce6124831b936bb4ef6015e470868a535f1c6e04f3afed8a1",
  "transaction_mroot": "0000000000000000000000000000000000000000000000000000000000000000",
  "action_mroot": "1bf9d17b5a951cbb6d0a8324e4039744db4137df498abd53046ea26fa74d73c9",
  "schedule_version": 1,
  "new_producers": null,
  "producer_signature": "SIG_K1_JxFfxGA1wZx9LCVjbrBb5nxTuJai7RUSiwRXyY866fYvZZyRtdmQFn9KJCqVHFAiYEsJpDb6dhTmHNDwipJm4rDiyhEmGa",
  "transactions": [],
  "id": "02e1c7888a92206573ae38d00e09366c7ba7bc54cd8b7996506f7d2a619c43ba",
  "block_num": 48351112,
  "ref_block_prefix": 3493375603
}
```

* Query the testnet to retrieve full block information about block ID `02e1c7888a92206573ae38d00e09366c7ba7bc54cd8b7996506f7d2a619c43ba`:

**Example Output**

```sh
fucli -u https://choiceofyourprovider get block 02e1c7888a92206573ae38d00e09366c7ba7bc54cd8b7996506f7d2a619c43ba
```
```json
{
  "timestamp": "2021-01-28T17:58:59.500",
  "producer": "inith",
  "confirmed": 0,
  "previous": "02e1c78787ff4d4ce6124831b936bb4ef6015e470868a535f1c6e04f3afed8a1",
  "transaction_mroot": "0000000000000000000000000000000000000000000000000000000000000000",
  "action_mroot": "1bf9d17b5a951cbb6d0a8324e4039744db4137df498abd53046ea26fa74d73c9",
  "schedule_version": 1,
  "new_producers": null,
  "producer_signature": "SIG_K1_JxFfxGA1wZx9LCVjbrBb5nxTuJai7RUSiwRXyY866fYvZZyRtdmQFn9KJCqVHFAiYEsJpDb6dhTmHNDwipJm4rDiyhEmGa",
  "transactions": [],
  "id": "02e1c7888a92206573ae38d00e09366c7ba7bc54cd8b7996506f7d2a619c43ba",
  "block_num": 48351112,
  "ref_block_prefix": 3493375603
}
```
