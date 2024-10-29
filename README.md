Qubitcoin Core integration/staging tree
=====================================

https://superquantum.io

For more information, see the blogposts
https://medium.com/@qubitcoin/boost-quantum-simulators-with-your-hashpower-c8142ad73894.

Quantum Proof of Work (qPoW)
------------------------------
The idea of Quantum Proof of Work (qPoW) opens up a unique opportunity: using blockchain tech to boost quantum simulators. This method might connect blockchain peer-to-peer competition and quantum computing features, speeding up growth in both areas.

The cryptocurrency mining ecosystem has already demonstrated its power to drive rapid innovation in specialized computing hardware and software. Bitcoin mining, for instance, evolved from CPU-based mining to GPU mining, then to FPGA mining, and finally to ASIC mining, all within a few years. It is an increase in computational efficiency of 10 million times, from 10 MH/s on CPUs to 100 TH/s on ASICs ([The Best Bitcoin Mining Machines in July 2024](https://tokentax.co/blog/best-bitcoin-mining-machines)). This progression was driven by the economic incentives inherent in the mining process.

At its heart, qPoW integrates a quantum computation challenge into the standard [Hashcash](http://www.hashcash.org/papers/hashcash.pdf) Proof of Work algorithm. An early [prototype of qPoW](https://arxiv.org/abs/2204.10643) was published in 2022 by Shalaginov and Dubrovsky. 

The version that we are deploying here contains a quantum challenge that involves discovering a set of maximum states of a pseudo-random quantum circuit with parametrized rotational single-qubit gates. While miners are cracking these quantum puzzles, they will be providing computational power to check and improve quantum algorithms.

The beauty of this system lies in its potential to create a large-scale, distributed network of competing quantum simulators. Each miner taking part in a qPoW-based blockchain would need to operate a quantum simulator to stay in the race. This will result in a huge boost in the combined strength and complexity of the world’s quantum simulation abilities. As miners fine-tune their quantum simulators to get ahead in mining, they would be expanding the limits of quantum simulation algorithms and hardware. The rivalry in cryptocurrency mining will spark advancements in simulation precision, efficiency, and growth potential. Moreover, qPoW has the potential to act as a standard benchmark for comparing the performance of quantum hardware and simulators.


Qubitcoin
---------
Qubitcoin takes a mixed approach merging cryptographic hash functions with quantum computing circuits. As a part of the cycle, miners have to complete a quantum task, check the outcomes, and then use these results in the following hashing step. This setup makes sure the whole system stays secure and that it can be verified on regular computers, while also pushing for the growth and use of quantum computing resources.

![figure1](figure1.png)
*Figure 1: Schematic of qPoW algorithm representing an extended Proof-of-Work cycle with a quantum task. The task is to compute the outputs of a circuit composed of single-qubit parameterized gates and two-qubit CNOTs applied to the neighboring qubits. At the circuit end, we record for each qubit a probability of being in the state 1, then convert it into bit-string format and merge them into an output 256-bit string. The bit-string is further XOR-multiplied with the initial hash, hashed by SHA3, and checked whether it satisfies the difficulty criterion.*

Currently, the qPoW hashing algorithm inside of Qubitcoin utilizes cuStateVec library from NVIDIA cuQuantum SDK to run quantum circuit simulations on GPUs. Hence, suitable versions of NVIDIA GPU driver, CUDA Toolkit, and cuQuantum itself, are required to build and/or run the full node. 

We use 128-bit complex floating-point numbers to maximize the precision of calculations. However, to ensure the consistency of quantum circuit computations across different backends, the final expectation values are converted to fixed-point fractional numbers. Hash functions in the qPoW algorithm are SHA256 which are imported directly from the original Bitcoin source code. As for the floating-to-fixed-point number conversions, the “fpm” open-source library is already included in the node implementation. After hashing the block data, each of the rotation gates in the quantum circuit is parameterized with 4 bit segments of the input hash. The final hashing step is performed on XOR-combination of the initial classical hash string and the string composed of concatenated Z-axis projected qubit expectation values in fixed-point notation.

Our mining package utilizes cuQuantum for simulating quantum circuits, but, as mentioned in the Bring Your Own Solver section, miners can use instead other quantum simulators, or, perhaps, ordinary linear algebra libraries. In BYOS case, one needs to implement the functionality to run and measure qPoW circuits using the desired software by creating the qhash_thread_init and run_simulation functions with appropriate signatures, as detailed in the qhash-gate.h header file, and use them instead of the provided ones by adding the new source files into the cpuminer_SOURCES variable in Makefile.am in place of the qhash-custatevec.c file. It would also be required to integrate any new dependencies into the build system by supplying the necessary compiler and linker flags in Makefile.am. CUSTATEVEC_INCLUDES and CUSTATEVEC_LDFLAGS variables demonstrate how it can be done for the cuStateVec library with CUDA runtime API. After that, the miner can be built and used as usual.

At the current moment, the algorithm implementation with the default cuQuantum backend can only run in one CPU thread. We will work on resolving this issue in future releases. In the meantime, it is advised to use custom solvers to achieve the maximum possible hashrates.

How to start mining Qubitcoin
-----------------------------
Verify that your GPU satisfies the requirements (computing capability >= 7) 
https://developer.nvidia.com/cuda-gpus

for Windows:
* install WSL (Ubuntu 24.04 LTS): https://learn.microsoft.com/en-us/windows/wsl/install
* install CUDA on WSL: https://docs.nvidia.com/cuda/wsl-user-guide/index.html
_make further actions in WSL_

for Linux (required Ubuntu 24.04 LTS):
* install CUDA:  https://www.liberiangeek.net/2024/04/install-cuda-on-ubuntu-24-04/

After installing CUDA:
* install libzmq5 and libevent
  * sudo apt update
  * sudo apt install -y libevent-dev
  * sudo apt install -y libzmq5

* download the executables (qubitcoind, qubitcoin-cli, qubitcoin-miner from [superquantum](https://superquantum.io/qubitcoin.html or build your own [see instructions below])
* locate the packages in a specified directory (e.g. ~/ or /home/username)
  * [Windows/WSL] copy files from download folder to /home: cp /mnt/c/Users/aardp/Downloads/qubitcoin ~/
* may need to change file format for the executables: chmod +x _file_name_ (e.g chmod +x qubitcoind)
* run the node executable (aka bitcoin daemon) qubitcoind (from the directory where the packages are located):
  sudo ./qubitcoind -rpcuser=test -rpcpassword=test -bind="0.0.0.0:42069" -addnode="73.47.180.210:42069" -reindex
  _here we specified the ip address of one of the available nodes_
* start/add a wallet (command line interface) in a separate terminal window [do it once when you create wallet for the first time]: 
  sudo ./qubitcoin-cli -rpcuser=test -rpcpassword=test createwallet {wallet_name}
  sudo ./qubitcoin-cli -rpcuser=test -rpcpassword=test getnewaddress
  sudo ./qubitcoin-cli -rpcuser=test -rpcpassword=test loadwallet {wallet_name}
* launch a miner using the wallet address acquired earlier 
  sudo ./qubitcoin-miner --algo qhash -t 1 --url http://127.0.0.1:8332 --userpass test:test --coinbase-addr {address from previous step}

After that the mining process should begin. The node and miner can be terminated by ctr+c.  
Check peer-info: 
  sudo ./qubitcoin-cli -rpcuser=test -rpcpassword=test getpeerinfo

Bitcoin Fork
------------

In developing Qubitcoin, to start we naturally chose to fork the Bitcoin codebase as our starting point. This decision was made to leverage Bitcoin’s battle-tested blockchain infrastructure. However, we are planning to make several key modifications to adapt the protocol for enabling qPoW:

~ Block time. We are going to adjust the target block time to account for the additional complexity introduced by the quantum computation task.

~ Difficulty adjustment. We plan to implement the ASERT (Absolutely Scheduled Exponentially Rising Targets) difficulty adjustment algorithm. ASERT provides more responsive and stable difficulty adjustments, which is crucial given the potential for rapid advancements in quantum computation capabilities.

~ Block reward. We want to modify the block reward structure to ensure long-term sustainability of the network while incentivizing early adoption and investment in quantum mining capabilities..

License
-------

Qubitcoin Core is released under the terms of the MIT license. See [COPYING](COPYING) for more information or see https://opensource.org/licenses/MIT.

Development Process
-------------------

The `master` branch is regularly built and tested, but it is not guaranteed to be
completely stable. Tags are created regularly from release branches to indicate new official, stable release versions of Qubitcoin Core.

The contribution workflow is described in CONTRIBUTING.md
and useful hints for developers can be found in doc/developer-notes.md.

Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

### Automated Testing

Developers are strongly encouraged to write unit-tests for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run
(assuming they weren't disabled in configure) with: `make check`. Further details on running
and extending unit tests can be found in /src/test/README.md.

There are also regression and integration tests, written
in Python.
These tests can be run (if the test dependencies are installed) with: `test/functional/test_runner.py`

The CI (Continuous Integration) systems make sure that every pull request is built for Windows, Linux, and macOS,
and that unit/sanity tests are run automatically.

### Manual Quality Assurance (QA) Testing

Changes should be tested by somebody other than the developer who wrote the
code. This is especially important for large or high-risk changes. It is useful
to add a test plan to the pull request description if testing the changes is
not straightforward.

Contributors
------------
* Daniil Shatokhin
* David Nizovsky
* Mikhail Shalaginov

