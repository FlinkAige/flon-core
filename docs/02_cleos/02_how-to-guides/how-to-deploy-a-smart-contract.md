## Goal

Deploy an FullOn contract

## Before you begin

* Install the currently supported version of `fulci`

* Unlock the wallet which contains the private key of the contract account

## Steps

Execute:

```sh
fulci set contract contract_account contract_folder [wasm-file] [abi-file]
```

Replace the `contract_folder` with the path that points to your contract folder

[[info | Default contract name]]
| By default, `fulci` treats the last folder specified in `contract_folder` as the contract name. Therefore, it expects `.wasm` and `.abi` files with that contract name as filename. This can be overridden with the optional `wasm-file` and `abi-file` parameters.
