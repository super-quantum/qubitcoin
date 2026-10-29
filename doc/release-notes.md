Qubitcoin Core Release Notes
============================

Notable changes
---------------

### New testnet

`-testnet` is now a Qubitcoin network of its own. It used to run on Bitcoin's testnet
genesis block, which does not meet its own target under qhash, so the node failed on
its second start.

| | |
|---|---|
| genesis | `00001e634001d70204b14676a716cd019b97e3cb6d3dd818a54a24e41feb39d8` |
| P2P magic | `d1 d4 c3 d4` |
| default port | 42070 |
| DNS seeds, fixed seeds, checkpoints | none |

Its data directory is `testnet/` rather than `testnet3/`; an existing `testnet3/`
directory is neither used nor touched. `-signet` still uses Bitcoin's genesis and does
not work yet.

### deqode on testnet

Testnet activates a second proof-of-work task at height 5: task 1, `deqode`, a
stabilizer decoding problem solved on the CPU. From then on both tasks target one block
every 1200 seconds. A task 1 block carries its solution in the block header. See
[deqode.md](deqode.md) for the specification; `qubitcoin-miner` can mine it. On
regtest the task can be enabled with `-deqodeheight=<n>`.

### Proof-of-work tasks

- A block version whose top three bits are `010` carries a proof-of-work task id in
  bits 20–27. Every other version, including those of all existing blocks, is task 0.
- Blocks of tasks other than 0 carry task data in the header after `nNonce`, returned
  as `taskdata` by `getblockheader` and `getblock`. Task 0 headers are unchanged.
  See [pow-tasks.md](pow-tasks.md).
- `getblocktemplate` accepts an optional `task_id` in the template request (default 0)
  and returns a `task` object describing the task.
- `getmininginfo` and `getblockchaininfo` list each task's difficulty and activation;
  `getdifficulty` and the `generate*` RPCs take an optional task id.
- Each block's chain work is the geometric mean of the scheduled work of all active
  tasks. With only task 0 active, as on mainnet, chain work is unchanged.
- Headers received during initial sync must have exactly the `nBits` the difficulty
  schedule requires.

### `-daemon`

`-daemon` works again. Computing the genesis block hash at startup initialised CUDA
before the process forked, so the daemon died right after starting.

### `qubitcoin-genesis`

A new tool that mines a genesis block for a given target and prints the lines needed in
the chain parameters:

```
src/qubitcoin-genesis --bits=0x1e40871a
```

---

The rest of this file is the upstream Bitcoin Core 27.1 release notes, inherited
with the fork and kept for reference.

27.1 Release Notes
=====================

Bitcoin Core version 27.1 is now available from:

  <https://bitcoincore.org/bin/bitcoin-core-27.1/>

This release includes various bug fixes and performance
improvements, as well as updated translations.

Please report bugs using the issue tracker at GitHub:

  <https://github.com/bitcoin/bitcoin/issues>

To receive security and update notifications, please subscribe to:

  <https://bitcoincore.org/en/list/announcements/join/>

How to Upgrade
==============

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes in some cases), then run the
installer (on Windows) or just copy over `/Applications/Bitcoin-Qt` (on macOS)
or `bitcoind`/`bitcoin-qt` (on Linux).

Upgrading directly from a version of Bitcoin Core that has reached its EOL is
possible, but it might take some time if the data directory needs to be migrated. Old
wallet versions of Bitcoin Core are generally supported.

Compatibility
==============

Bitcoin Core is supported and extensively tested on operating systems
using the Linux Kernel 3.17+, macOS 11.0+, and Windows 7 and newer. Bitcoin
Core should also work on most other Unix-like systems but is not as
frequently tested on them. It is not recommended to use Bitcoin Core on
unsupported systems.

Notable changes
===============

### Miniscript

- #29853 sign: don't assume we are parsing a sane TapMiniscript

### RPC

- #29869 rpc, bugfix: Enforce maximum value for setmocktime
- #29870 rpc: Reword SighashFromStr error message
- #30094 rpc: move UniValue in blockToJSON

### Index

- #29776 Fix #29767, set m_synced = true after Commit()

### Gui

- #gui812 Fix create unsigned transaction fee bump
- #gui813 Don't permit port in proxy IP option

### Test

- #29892 test: Fix failing univalue float test

### P2P

- #30085 p2p: detect addnode cjdns peers in GetAddedNodeInfo()

### Build

- #29747 depends: fix mingw-w64 Qt DEBUG=1 build
- #29859 build: Fix false positive CHECK_ATOMIC test
- #29985 depends: Fix build of Qt for 32-bit platforms with recent glibc
- #30097 crypto: disable asan for sha256_sse4 with clang and -O0
- #30151 depends: Fetch miniupnpc sources from an alternative website
- #30216 build: Fix building fuzz binary on on SunOS / illumos
- #30217 depends: Update Boost download link

### Doc

- #29934 doc: add LLVM instruction for macOS < 13

### CI

- #29856 ci: Bump s390x to ubuntu:24.04

### Misc

- #29691 Change Luke Dashjr seed to dashjr-list-of-p2p-nodes.us
- #30149 contrib: Renew Windows code signing certificate

Credits
=======

Thanks to everyone who directly contributed to this release:

- Antoine Poinsot
- Ava Chow
- Cory Fields
- dergoegge
- fanquake
- furszy
- Hennadii Stepanov
- Jon Atack
- laanwj
- Luke Dashjr
- MarcoFalke
- nanlour
- Sjors Provoost
- willcl-ark

As well as to everyone that helped with translations on
[Transifex](https://www.transifex.com/bitcoin/bitcoin/).
