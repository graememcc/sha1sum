#include <vector>
#include <cinttypes>
#include <iostream>
#include <numeric>
#include <iomanip>
#include <functional>
#include <fstream>
#include <algorithm>
#include <limits>


namespace {
constexpr auto blockBits = 512;

constexpr auto int32Size = sizeof(uint32_t);
constexpr int int32Bits = std::numeric_limits<uint32_t>::digits;
constexpr auto intsPerBlock = blockBits / int32Bits;

constexpr auto resultBits = 160;
constexpr auto intsInResult = resultBits / int32Bits;

using BlockVector = std::vector<uint32_t>;
using HashVector = std::vector<uint32_t>;


uint32_t rotl(const uint32_t x, const unsigned int n) {
  const unsigned int rshift = int32Bits - n;
  return (x << n) | (x >> rshift);
}


uint32_t accumulateUint(const uint32_t left, unsigned char right) {
  return (left << 8) | right;
}


typedef std::function<uint32_t(const uint32_t, const uint32_t, const uint32_t)> RoundFunction;


RoundFunction& getFunctionForRound(const unsigned int round) {
  static RoundFunction roundFunctions[] = {
    // Ch: rounds 0 - 19
    [] (const uint32_t& x, const uint32_t& y, const uint32_t& z) -> uint32_t {
      return (x & y) ^ (~x & z);
    },

    // Parity: 20-39, 60 - 79
    [] (const uint32_t& x, const uint32_t& y, const uint32_t& z) -> uint32_t {
      return x ^ y ^ z;
    },

    // Maj: 40 - 59
    [] (const uint32_t& x, const uint32_t& y, const uint32_t& z) -> uint32_t {
      return (x & y) ^ (x & z) ^ (y & z);
    }
  };

  return roundFunctions[(round > 59 ? round - 40 : round) / 20];
}


const uint32_t& getConstantForRound(const unsigned int round) {
  static uint32_t constants[] = {
    0x5a827999, // 0 - 29
    0x6ed9eba1, // 20 - 39
    0x8f1bbcdc, // 40 - 59
    0xca62c1d6, // 60 - 79
  };

  return constants[round / 20];
}


struct RoundVariables {
  HashVector workingVars;
  BlockVector W;

  RoundVariables(HashVector& hash, BlockVector&& w) : workingVars(HashVector(hash)), W(w) {}
  HashVector getHash() { return workingVars; }
};


void hashRound(RoundVariables& roundVars, const unsigned int round) {
  enum {a, b, c, d, e};
  HashVector& vars = roundVars.workingVars;

  uint32_t T = 0u;

  T = rotl(vars[a], 5) + getFunctionForRound(round)(vars[b], vars[c], vars[d]) +
           vars[e] + getConstantForRound(round) + roundVars.W[round];

  std::rotate(vars.rbegin(), vars.rbegin() + 1, vars.rend());
  vars[c] = rotl(vars[c], 30);
  vars[a] = T;
}


BlockVector computeHash(HashVector&& previousHash, const BlockVector::const_iterator& blockStart, const BlockVector::const_iterator& blockEnd) {
  BlockVector w;

  std::copy(blockStart, blockEnd, std::back_inserter(w));

  for (auto i = 16; i < 80; i++) {
    w.emplace_back(rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1));
  }

  RoundVariables roundVars(previousHash, std::move(w));
  for (auto i = 0; i < 80; i++) {
    hashRound(roundVars, i);
  }

  HashVector newHash = roundVars.getHash();
  std::transform(newHash.begin(), newHash.end(), previousHash.begin(), newHash.begin(),
                 std::plus<uint32_t>());

  return newHash;
}


HashVector sha1(const BlockVector& input) {
  HashVector hashes = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};

  for (auto it = input.begin(); it < input.end(); it += intsPerBlock) {
    hashes = computeHash(std::move(hashes), it, it + intsPerBlock);
  }

  return hashes;
}


typedef std::pair<BlockVector, uint64_t> Input;


BlockVector PadInput(Input&& input) {
  BlockVector padded(std::get<0>(input));
  uint64_t byteCount = std::get<1>(input);

  // Add the end-of-message marker
  if ((byteCount % int32Size) != 0) {
    unsigned char marker = 0x80u;

    uint32_t last = padded.back();
    last = accumulateUint(last, marker);
    last = last << ((int32Size - (byteCount % int32Size) - 1) * 8);

    padded.back() = last;
  } else {
    padded.emplace_back(0x80000000);
  }

  unsigned int paddingRequired = intsPerBlock - ((padded.size() + 2) % intsPerBlock);
  padded.insert(padded.end(), paddingRequired, '\0');

  // Add the number of bits in the original message
  byteCount *= 8;
  uint32_t messageLenLSB = static_cast<uint32_t>(byteCount & 0xFFFFFFFF);
  uint32_t messageLenMSB = static_cast<uint32_t>((byteCount >> (int32Size * 8)) & 0xFFFFFFFF);

  // The standard assumes big-endian
  padded.emplace_back(messageLenMSB);
  padded.emplace_back(messageLenLSB);

  return padded;
}


Input GetInput(std::istream& in) {
  int c;
  uint64_t count = 0u;
  uint32_t next = 0u;

  BlockVector v;
  v.reserve(intsPerBlock);

  while ((c = in.get()) != EOF) {
    unsigned char charInput = static_cast<unsigned char>(c);
    next = accumulateUint(next, charInput);

    if ((++count % int32Size) == 0) {
      v.emplace_back(next);
      next = 0;
    }
  }

  if (count > 0 && (count % int32Size) != 0) {
    v.emplace_back(next);
  }

  return std::make_pair(std::move(v), count);
}


void hashSource(std::istream& in, const char* source) {
  HashVector hashed = sha1(PadInput(move(GetInput(in))));

  std::cout << std::hex << std::setfill('0') << std::setw(int32Size);
  for (auto i : hashed) {
    std::cout << i;
  }

  std::cout << "  " << source << std::endl;
}


} // namespace


int main(int argc, char** argv) {
  if (argc == 1) {
    hashSource(std::cin, "-");
    return 0;
  }

  int exitCode = 0;

  for (auto i = 1; i < argc; i++) {
    std::ifstream in(argv[i]);

    if (in.fail()) {
      std::cerr << argv[0] << ": " << argv[i] << ": no such file or directory" << std::endl;
      in.close();
      exitCode = 1;
      continue;
    }

    hashSource(in, argv[i]);
    in.close();
  }

  return exitCode;
}
