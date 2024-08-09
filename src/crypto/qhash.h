#ifndef QUBITCOIN_CRYPTO_QHASH_H
#define QUBITCOIN_CRYPTO_QHASH_H

#include <crypto/fpm/fixed.hpp>
#include <crypto/sha256.h>
#include <cstdint>
#include <cuComplex.h>
#include <custatevec.h>

class QHash
{
private:
    CSHA256 ctx;
    custatevecHandle_t handle; // TODO: should probably be a singleton

    static const size_t nQubits = 16;
    static const size_t nLayers = 2; // TODO

    using fixedFloat = fpm::fixed<int16_t, int32_t, 15>;

    std::array<double, nQubits> runSimulation(const std::array<unsigned char, 2 * CSHA256::OUTPUT_SIZE>& data);
    void runCircuit(cuComplex* dStateVec, const std::array<unsigned char, 2 * CSHA256::OUTPUT_SIZE>& data);
    std::array<double, nQubits> getExpectations(cuComplex* dStateVec);

public:
    static const size_t OUTPUT_SIZE = CSHA256::OUTPUT_SIZE;

    QHash();
    QHash& Write(const unsigned char* data, size_t len);
    void Finalize(unsigned char hash[OUTPUT_SIZE]);
    QHash& Reset();
    ~QHash();
};

template <size_t N>
std::array<unsigned char, 2 * N> splitNibbles(const std::array<unsigned char, N>& input)
{
    std::array<unsigned char, 2 * N> output;
    static const unsigned char nibbleMask = 0xF;
    for (size_t i = 0; i < N; ++i) {
        output[2 * i] = (input[i] >> 4) & nibbleMask;
        output[2 * i + 1] = input[i] & nibbleMask;
    }
    return output;
}

#endif