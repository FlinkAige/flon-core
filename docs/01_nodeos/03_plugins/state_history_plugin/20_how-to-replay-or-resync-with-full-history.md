---
content_title: How to replay or resync with full history
---

## Goal

This procedure records the entire chain history.

## Before you begin

* Make sure [FullOn is installed](../../../00_install/index.md).
* Learn about [Using funod](../../02_usage/index.md).
* Get familiar with [state_history_plugin](../../03_plugins/state_history_plugin/index.md).

## Steps

1. Get a block log and place it in `data/blocks`, or get a genesis file and use the `--genesis-json` option

2. Make sure `data/state` does not exist, or use the `--replay-blockchain` option

3. Start `funod` with the options listed in the [`state_history_plugin`](index.md)
