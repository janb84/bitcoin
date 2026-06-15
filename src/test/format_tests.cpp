// Copyright (c) 2025-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <serialization/format.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <boost/test/unit_test.hpp>

using namespace serialization;

namespace {

//! A trivial Writer: appends every byte run to a growable buffer.
struct VecWriter {
    std::vector<std::byte> bytes;
    void write(std::span<const std::byte> in) { bytes.insert(bytes.end(), in.begin(), in.end()); }
};

//! A trivial Reader over an in-memory buffer. Returns at most `chunk` bytes per
//! call so the test also exercises the "partial reads are normal" contract.
struct SliceReader {
    std::span<const std::byte> data;
    std::size_t chunk;
    std::size_t read_some(std::span<std::byte> out)
    {
        auto n = std::min({out.size(), data.size(), chunk});
        std::memcpy(out.data(), data.data(), n);
        data = data.subspan(n);
        return n;
    }
};

CMutableTransaction MakeWitnessTx()
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.nLockTime = 0x11223344;

    CTxIn in;
    in.prevout = COutPoint{Txid::FromUint256(uint256{"0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20"}), 7};
    in.scriptSig = CScript() << OP_1 << OP_2;
    in.nSequence = 0xfffffffe;
    in.scriptWitness.stack.push_back({0xde, 0xad, 0xbe, 0xef});
    in.scriptWitness.stack.push_back({});
    tx.vin.push_back(in);

    CTxOut out;
    out.nValue = 123456789;
    out.scriptPubKey = CScript() << OP_DUP << OP_HASH160;
    tx.vout.push_back(out);

    return tx;
}

template <typename T>
std::vector<std::byte> LegacyTxBytes(const T& tx)
{
    DataStream ss;
    ss << TX_WITH_WITNESS(tx);
    std::vector<std::byte> out(ss.size());
    std::memcpy(out.data(), ss.data(), ss.size());
    return out;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(format_tests, BasicTestingSetup)

// The new encoder must produce byte-for-byte the same wire format as the
// existing stream-based serializer (the format is the contract).
BOOST_AUTO_TEST_CASE(encode_matches_legacy_tx)
{
    const CTransaction tx{MakeWitnessTx()};

    VecWriter w;
    serialize(tx, w);

    BOOST_CHECK(w.bytes == LegacyTxBytes(tx));
}

BOOST_AUTO_TEST_CASE(encode_matches_legacy_header)
{
    CBlockHeader h;
    h.nVersion = 0x20000000;
    h.hashPrevBlock = uint256{"00000000000000000001a2b3c4d5e6f700000000000000000000000000000000"};
    h.hashMerkleRoot = uint256{"deadbeef00000000000000000000000000000000000000000000000000000000"};
    h.nTime = 1700000000;
    h.nBits = 0x1d00ffff;
    h.nNonce = 0x01020304;

    DataStream ss;
    ss << h;
    std::vector<std::byte> legacy(ss.size());
    std::memcpy(legacy.data(), ss.data(), ss.size());

    VecWriter w;
    serialize(h, w);
    BOOST_CHECK(w.bytes == legacy);
}

// Round-trip: encode with the new encoder, decode with the new decoder, and the
// transaction (including its witness) must come back identical.
BOOST_AUTO_TEST_CASE(roundtrip_tx)
{
    const CTransaction original{MakeWitnessTx()};

    VecWriter w;
    serialize(original, w);

    SliceReader r{w.bytes, 3}; // 3-byte chunks to force partial reads
    auto decoded = parse_transaction(r);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK(CTransaction{*decoded} == original);
    BOOST_CHECK(decoded->HasWitness());
}

BOOST_AUTO_TEST_CASE(roundtrip_block)
{
    CBlock block;
    block.nVersion = 0x20000000;
    block.hashPrevBlock = uint256{"00000000000000000001a2b3c4d5e6f700000000000000000000000000000000"};
    block.hashMerkleRoot = uint256{"deadbeef00000000000000000000000000000000000000000000000000000000"};
    block.nTime = 1700000000;
    block.nBits = 0x1d00ffff;
    block.nNonce = 5;
    block.vtx.push_back(MakeTransactionRef(MakeWitnessTx()));
    block.vtx.push_back(MakeTransactionRef(MakeWitnessTx()));

    // Byte-equivalence with the legacy block serializer.
    BOOST_CHECK(([&] {
        VecWriter w;
        serialize(block, w);
        return w.bytes == LegacyTxBytes(block);
    }()));

    VecWriter w;
    serialize(block, w);
    SliceReader r{w.bytes, 7};
    auto decoded = parse_block(r);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_REQUIRE_EQUAL(decoded->vtx.size(), block.vtx.size());
    BOOST_CHECK(decoded->GetHash() == block.GetHash());
    for (size_t i = 0; i < block.vtx.size(); ++i) {
        BOOST_CHECK(decoded->vtx[i]->GetWitnessHash() == block.vtx[i]->GetWitnessHash());
    }
}

// Truncated input is routine, not exceptional: it yields nullopt.
BOOST_AUTO_TEST_CASE(truncated_input_fails_gracefully)
{
    CBlockHeader h;
    h.nBits = 1;
    VecWriter w;
    serialize(h, w);

    w.bytes.resize(w.bytes.size() - 1); // chop off the last byte
    SliceReader r{w.bytes, 100};
    BOOST_CHECK(!parse_block_header(r).has_value());
}

// Non-canonical CompactSize (a value that should have used a shorter encoding)
// must be rejected, matching consensus rules.
BOOST_AUTO_TEST_CASE(non_canonical_size_rejected)
{
    // version=2 (4 bytes LE), then a vin count of 5 written non-canonically as
    // the 3-byte 0xfd 05 00 form instead of the single byte 0x05.
    std::vector<std::byte> buf;
    auto push = [&](std::initializer_list<uint8_t> v) {
        for (auto b : v) buf.push_back(std::byte{b});
    };
    push({0x02, 0x00, 0x00, 0x00}); // version
    push({0xfd, 0x05, 0x00});       // non-canonical CompactSize(5)

    SliceReader r{buf, 100};
    BOOST_CHECK(!parse_transaction(r).has_value());
}

// A hostile length field must not trigger a huge speculative allocation: the
// decoder grows incrementally and simply fails when the bytes run out.
BOOST_AUTO_TEST_CASE(no_speculative_allocation)
{
    std::vector<std::byte> buf;
    auto push = [&](std::initializer_list<uint8_t> v) {
        for (auto b : v) buf.push_back(std::byte{b});
    };
    push({0x02, 0x00, 0x00, 0x00}); // version
    // vin count = 0x00ffffff (< MAX_SIZE) via canonical 0xfe form, but no
    // element bytes follow at all.
    push({0xfe, 0xff, 0xff, 0xff, 0x00});

    SliceReader r{buf, 100};
    auto decoded = parse_transaction(r); // must return promptly, no OOM
    BOOST_CHECK(!decoded.has_value());
}

BOOST_AUTO_TEST_SUITE_END()
