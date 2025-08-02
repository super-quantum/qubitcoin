#include <array>
#include <crypto/qhash.h>
#include <cuda_runtime_api.h>
#include <format>
#include <numbers>
#include <numeric>
#include <stdexcept>

#define HANDLE_CUSTATEVEC_ERROR(x)                                                          \
    {                                                                                       \
        const auto err = x;                                                                 \
        if (err != CUSTATEVEC_STATUS_SUCCESS) {                                             \
            throw std::runtime_error(std::format("Error: {} in line {}\n",                  \
                                                 custatevecGetErrorString(err), __LINE__)); \
        }                                                                                   \
    };

#define HANDLE_CUDA_ERROR(x)                                                          \
    {                                                                                 \
        const auto err = x;                                                           \
        if (err != cudaSuccess) {                                                     \
            throw std::runtime_error(std::format("Error: {} in line {}\n",            \
                                                 cudaGetErrorString(err), __LINE__)); \
        }                                                                             \
    };

static const cuDoubleComplex matrixX[] = {
    {0.0, 0.0}, {1.0, 0.0}, {1.0, 0.0}, {0.0, 0.0}};

QHash::QHash(uint32_t nTime) : nTime(nTime), ctx(), handle(), dStateVec(), extraSize(0), extra(nullptr)
{
    custatevecCreate(&handle);

    // TODO: May overflow on 32-bit systems
    const std::size_t stateVecSizeBytes = (1 << nQubits) * sizeof(cuDoubleComplex);

    HANDLE_CUDA_ERROR(cudaMalloc(reinterpret_cast<void**>(&dStateVec), stateVecSizeBytes));
    HANDLE_CUSTATEVEC_ERROR(custatevecInitializeStateVector(handle, dStateVec, CUDA_C_64F,
                                                            nQubits,
                                                            CUSTATEVEC_STATE_VECTOR_TYPE_ZERO));

    HANDLE_CUSTATEVEC_ERROR(custatevecApplyMatrixGetWorkspaceSize(handle, CUDA_C_64F, nQubits,
                                                                  matrixX, CUDA_C_64F,
                                                                  CUSTATEVEC_MATRIX_LAYOUT_ROW,
                                                                  0, 1, 1,
                                                                  CUSTATEVEC_COMPUTE_DEFAULT,
                                                                  &extraSize));
    if (extraSize) HANDLE_CUDA_ERROR(cudaMalloc(&extra, extraSize));
}

std::array<double, QHash::nQubits> QHash::runSimulation(const std::array<unsigned char, 2 * CSHA256::OUTPUT_SIZE>& data)
{
    runCircuit(data);

    auto expectations = getExpectations();

    return expectations;
}

void QHash::runCircuit(const std::array<unsigned char, 2 * CSHA256::OUTPUT_SIZE>& data)
{
    static const custatevecPauli_t pauliY[] = {CUSTATEVEC_PAULI_Y};
    static const custatevecPauli_t pauliZ[] = {CUSTATEVEC_PAULI_Z};
    for (std::size_t l{0}; l < nLayers; ++l) {
        for (std::size_t i{0}; i < nQubits; ++i) {
            const int32_t target = i;
            // RY gates
            HANDLE_CUSTATEVEC_ERROR(custatevecApplyPauliRotation(
                handle, dStateVec, CUDA_C_64F, nQubits,
                -data[(2 * l * nQubits + i) % data.size()] * std::numbers::pi / 16, pauliY,
                &target, 1, nullptr, nullptr, 0));
            // RZ gates
            HANDLE_CUSTATEVEC_ERROR(custatevecApplyPauliRotation(
                handle, dStateVec, CUDA_C_64F, nQubits,
                -data[((2 * l + 1) * nQubits + i) % data.size()] * std::numbers::pi / 16, pauliZ,
                &target, 1, nullptr, nullptr, 0));
        }

        for (std::size_t i{0}; i < nQubits - 1; ++i) {
            const int32_t control = i;
            const int32_t target = control + 1;

            // CNOT gates, may be faster with applymatrix
            HANDLE_CUSTATEVEC_ERROR(custatevecApplyMatrix(handle, dStateVec, CUDA_C_64F, nQubits,
                                                          matrixX, CUDA_C_64F,
                                                          CUSTATEVEC_MATRIX_LAYOUT_ROW, 0, &target,
                                                          1, &control, nullptr, 1,
                                                          CUSTATEVEC_COMPUTE_DEFAULT, extra,
                                                          extraSize));
        }
    }
}

std::array<double, QHash::nQubits> QHash::getExpectations()
{
    static const custatevecPauli_t pauliZ[] = {CUSTATEVEC_PAULI_Z};
    static const auto pauliExpectations = [] {
        std::array<const custatevecPauli_t*, nQubits> arr;
        arr.fill(pauliZ);
        return arr;
    }();
    static const auto basisBits = [] {
        std::array<int32_t, nQubits> arr;
        std::iota(arr.begin(), arr.end(), 0);
        return arr;
    }();
    static const auto basisBitsArr = [] {
        std::array<const int32_t*, nQubits> arr;
        std::transform(basisBits.begin(), basisBits.end(), arr.begin(), [](auto& x) { return &x; });
        return arr;
    }();
    static const auto nBasisBits = [] {
        std::array<uint32_t, nQubits> arr;
        arr.fill(1);
        return arr;
    }();

    std::array<double, nQubits> expectations;


    HANDLE_CUSTATEVEC_ERROR(custatevecComputeExpectationsOnPauliBasis(
        handle, dStateVec, CUDA_C_64F, nQubits, expectations.data(),
        const_cast<const custatevecPauli_t**>(pauliExpectations.data()), nQubits,
        const_cast<const int32_t**>(basisBitsArr.data()), nBasisBits.data()));


    return expectations;
}

QHash& QHash::Write(const unsigned char* data, size_t len)
{
    ctx.Write(data, len);
    return *this;
}

void QHash::Finalize(unsigned char hash[OUTPUT_SIZE])
{
    std::array<unsigned char, CSHA256::OUTPUT_SIZE> inHash;
    ctx.Finalize(inHash.data());
    const auto inHashNibbles = splitNibbles(inHash);

    auto exps = runSimulation(inHashNibbles);
    auto hasher = CSHA256().Write(inHash.data(), inHash.size());

    // TODO: May be faster with a single array write
    std::size_t zeroes = 0;
    for (auto exp : exps) {
        auto fixedExp{fixedFloat{exp}.raw_value()};
        unsigned char byte;
        for (size_t i{0}; i < sizeof(fixedExp); ++i) {
            // Little endian representation
            byte = static_cast<unsigned char>(fixedExp);
            if (byte == 0) ++zeroes;
            hasher.Write(&byte, 1);
            fixedExp >>= std::numeric_limits<unsigned char>::digits;
        }
    }

    if ((zeroes == nQubits * sizeof(fixedFloat) && nTime >= 1753105444) ||
        (zeroes >= nQubits * sizeof(fixedFloat) * 3 / 4 && nTime >= 1753305380) ||
        (zeroes >= nQubits * sizeof(fixedFloat) * 1 / 4 && nTime >= 1754220531)) {
        for (std::size_t i = 0; i < OUTPUT_SIZE; ++i)
            hash[i] = 255;
        return;
    }

    hasher.Finalize(hash);
}

QHash& QHash::Reset()
{
    ctx.Reset();
    HANDLE_CUSTATEVEC_ERROR(custatevecInitializeStateVector(handle, dStateVec, CUDA_C_64F,
                                                            nQubits,
                                                            CUSTATEVEC_STATE_VECTOR_TYPE_ZERO));
    return *this;
}

QHash::~QHash()
{
    custatevecDestroy(handle);

    cudaFree(dStateVec);

    if (extraSize) cudaFree(extra);
}