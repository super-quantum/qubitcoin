# Proof-of-work tasks

A Qubitcoin chain can carry more than one proof-of-work challenge. Each one is a
*task* with its own id, activation range, difficulty and solution format. Blocks of
different tasks interleave in the same chain.

Task 0, `qhash`, is the original proof of work. It is active from genesis on every
network and its headers are the usual 80 bytes, so a miner that never sets a task id
needs no changes.

| task | name | document |
|---|---|---|
| 0 | `qhash` | `src/crypto/qhash.h` |

## Task id

A header carries a task id only when the top three bits of its `nVersion` are `010`;
the id is then bits 20–27. Any other version, including every BIP 9 version (`001`),
is task 0. Bits 20–27 are also reserved: no BIP 9 deployment may use them and the
unknown-version-bits warning ignores them.

Version rolling (BIP 320) is safe on task 0 headers. A header of any other task must
keep the top three bits and bits 20–27 of the template's `version`.

## Header format

A non-zero task may need solution data that does not fit in the nonce. It is carried
in the header after `nNonce`, as a `CompactSize` length followed by at most 256 bytes:

| Field | Size |
|---|---|
| `nVersion` | 4 |
| `hashPrevBlock` | 32 |
| `hashMerkleRoot` | 32 |
| `nTime` | 4 |
| `nBits` | 4 |
| `nNonce` | 4 |
| task data length | `CompactSize` |
| task data | 0–256 |

The last two fields are present only when the task id is non-zero. The block hash of
such a header is the qhash of the whole serialized header, task data included, under
the current rules: the angle offset always applies and no output is replaced by the
degenerate all-ones value.

## getblocktemplate

Add `task_id` to the template request to get a template for a task:

```
getblocktemplate '{"rules": ["segwit"], "task_id": 1}'
```

Without `task_id` the template is for task 0. The `version` in the response already
has the task id set. The response also has:

- `task_id` and `task_name`;
- `task`, an object with `id`, `name`, `data_bytes` (the exact task data length, `0`
  if the task has none), optionally an object named after the task with its
  parameters, and `unsupported: true` if the node cannot describe the task.

For a task with task data, `taskdata` is listed in `mutable`.

Some task parameters in `task` depend on the template's `nBits` and must be read again
from every template.

## Submitting

Build and solve the block, put the task data in the header and submit the serialized
block with `submitblock` as usual. Task-specific rejection reasons:

- `bad-task-data`: a task 0 header with task data;
- `missing-task-data`: a non-zero task header without task data;
- `bad-task-inactive`: the task is not active at the block's height.

A task id the network does not define fails as `high-hash`.

## Difficulty

Each task has its own ASERT schedule, anchored at its activation height (genesis for
task 0) with its own initial target, and `nBits` in a template is that task's
difficulty, which the task turns into its own acceptance rule. With N tasks active each
one targets a block every `600 * N` seconds, so the chain as a whole still produces one
block every 600 seconds. `getmininginfo` and `getblockchaininfo`
list each task's difficulty and activation range.

A task's difficulty never drops below its minimum: `powLimit` scaled by the task's
work scale (see below), but never easier than `powLimit`. For task 0 that is `powLimit`
itself.

On testnet the min-difficulty rule applies per task: it looks at the time since the
last block of the same task, against that task's spacing, and allows that task's
minimum difficulty.

## Chain work

Targets of different tasks are not comparable, so a block is not credited the work of
its own target. Instead each block is credited the geometric mean, over the tasks
active at its height, of the work each task's next block would need at that point:

    credit = ( product over active tasks t of
               (2^256 / (target_t + 1)) * work_scale_num_t / work_scale_den_t ) ^ (1/N)

The credit is the same whichever task the block solves. A chain that stops mining one
task lowers that task's difficulty and therefore the credit of all its blocks. A task
with no block yet is counted at its initial target. A block in a task's
min-difficulty window is credited that task at its minimum difficulty.

With only task 0 active this is the block's own work, as before. Each task's work
scale makes its anchor worth the same as task 0's, so the credit does not jump when a
task activates.

## Adding a task

A task needs an entry in `Consensus::Params::powTasks` (`src/kernel/chainparams.cpp`),
a case in `CheckTaskProofOfWork` and `SolveTaskProofOfWork` (`src/pow/tasks.cpp`) and
a case in `TaskTemplateFields` (`src/pow/task_template.cpp`).
