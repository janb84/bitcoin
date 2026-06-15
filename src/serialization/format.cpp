// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <serialization/format.h>

#include <crypto/common.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <serialize.h> // for MAX_SIZE / MAX_VECTOR_ALLOCATE
#include <uint256.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <ranges>
#include <vector>

// This translation unit is the single compilation boundary for the format.
// Everything below (the encoders, decoders, buffering and the failure-state
// decoder context) is private to this file; only the entry points declared in
// the header are exported. The encoders and decoders are composable function
// objects (constexpr lambdas) rather than methods, so they are decoupled from
// the data structures they happen to operate on: the format, not the class
// layout, is the source of truth.

namespace serialization {
namespace {

// -- Writer side ------------------------------------------------------------

// Buffers small writes so the per-call indirection through writer_ref is paid
// once per buffer flush rather than once per field.
template <Writer W>
class BufferedWriter
{
public:
    explicit BufferedWriter(W& w) : m_w{w} {}
    ~BufferedWriter() { flush(); }

    void write(std::span<const std::byte> bytes)
    {
        // Large writes bypass the buffer to avoid pointless copying.
        if (bytes.size() >= m_buffer.size()) {
            flush();
            m_w.write(bytes);
            return;
        }
        if (m_pos + bytes.size() > m_buffer.size()) {
            flush();
        }
        std::memcpy(m_buffer.data() + m_pos, bytes.data(), bytes.size());
        m_pos += bytes.size();
    }

    void flush()
    {
        if (m_pos != 0) {
            m_w.write(std::span{m_buffer}.first(m_pos));
            m_pos = 0;
        }
    }

private:
    W& m_w;
    std::array<std::byte, 4096> m_buffer;
    std::size_t m_pos = 0;
};

inline constexpr auto encode_u8 = [](auto& w, std::uint8_t v) {
    auto b = static_cast<std::byte>(v);
    w.write(std::span{&b, 1});
};

inline constexpr auto encode_u16 = [](auto& w, std::uint16_t v) {
    std::array<std::byte, 2> b;
    WriteLE16(b.data(), v);
    w.write(b);
};

inline constexpr auto encode_u32 = [](auto& w, std::uint32_t v) {
    std::array<std::byte, 4> b;
    WriteLE32(b.data(), v);
    w.write(b);
};

inline constexpr auto encode_u64 = [](auto& w, std::uint64_t v) {
    std::array<std::byte, 8> b;
    WriteLE64(b.data(), v);
    w.write(b);
};

// Accepts any 32-byte blob exposing data()/size() (uint256, Txid, ...).
inline constexpr auto encode_hash256 = [](auto& w, const auto& v) {
    w.write(std::as_bytes(std::span{v.data(), v.size()}));
};

// CompactSize, written in its canonical (shortest) form.
inline constexpr auto encode_size = [](auto& w, std::size_t v) {
    if (v < 253) {
        encode_u8(w, static_cast<std::uint8_t>(v));
    } else if (v <= 0xFFFF) {
        encode_u8(w, 253);
        encode_u16(w, static_cast<std::uint16_t>(v));
    } else if (v <= 0xFFFF'FFFF) {
        encode_u8(w, 254);
        encode_u32(w, static_cast<std::uint32_t>(v));
    } else {
        encode_u8(w, 255);
        encode_u64(w, static_cast<std::uint64_t>(v));
    }
};

inline constexpr auto encode_bytes = [](auto& w, std::span<const std::byte> bytes) {
    encode_size(w, bytes.size());
    w.write(bytes);
};

// Encodes a length-prefixed sequence using a per-element encoder.
inline constexpr auto encode_range = [](auto& w, const auto& range, auto encode_elem) {
    encode_size(w, std::ranges::size(range));
    for (const auto& elem : range) {
        encode_elem(w, elem);
    }
};

inline constexpr auto as_byte_span = [](const auto& container) {
    return std::as_bytes(std::span{container.data(), container.size()});
};

inline constexpr auto encode_outpoint = [](auto& w, const COutPoint& outpoint) {
    encode_hash256(w, outpoint.hash);
    encode_u32(w, outpoint.n);
};

inline constexpr auto encode_txin = [](auto& w, const CTxIn& txin) {
    encode_outpoint(w, txin.prevout);
    encode_bytes(w, as_byte_span(txin.scriptSig));
    encode_u32(w, txin.nSequence);
};

inline constexpr auto encode_txout = [](auto& w, const CTxOut& txout) {
    encode_u64(w, static_cast<std::uint64_t>(txout.nValue));
    encode_bytes(w, as_byte_span(txout.scriptPubKey));
};

inline constexpr auto encode_witness_item = [](auto& w, const std::vector<unsigned char>& item) {
    encode_bytes(w, as_byte_span(item));
};

inline constexpr auto encode_header = [](auto& w, const CBlockHeader& h) {
    encode_u32(w, static_cast<std::uint32_t>(h.nVersion));
    encode_hash256(w, h.hashPrevBlock);
    encode_hash256(w, h.hashMerkleRoot);
    encode_u32(w, h.nTime);
    encode_u32(w, h.nBits);
    encode_u32(w, h.nNonce);
};

// The transaction encoder is parameterised by the witness policy, returning a
// concrete encoder. This keeps the witness branch a property of the format
// rather than a runtime flag threaded through a stream object.
inline constexpr auto encode_tx = [](witness wmode) {
    return [=](auto& w, const CTransaction& tx) {
        const bool with_witness = (wmode == witness::allow) && tx.HasWitness();

        encode_u32(w, tx.version);

        if (with_witness) {
            encode_u8(w, 0); // dummy
            encode_u8(w, 1); // flags
        }

        encode_range(w, tx.vin, encode_txin);
        encode_range(w, tx.vout, encode_txout);

        if (with_witness) {
            for (const auto& in : tx.vin) {
                encode_range(w, in.scriptWitness.stack, encode_witness_item);
            }
        }

        encode_u32(w, tx.nLockTime);
    };
};

inline constexpr auto encode_block = [](witness wmode) {
    return [=](auto& w, const CBlock& block) {
        encode_header(w, block);
        encode_range(w, block.vtx, [enc = encode_tx(wmode)](auto& ww, const CTransactionRef& tx) {
            enc(ww, *tx);
        });
    };
};

// -- Reader side ------------------------------------------------------------

// Buffers reads from the (indirect) underlying reader; see blog part 2.
template <Reader R, std::size_t S = 4096>
class buffered_reader
{
public:
    explicit buffered_reader(R& r) : m_r{r} {}

    std::size_t read_some(std::span<std::byte> out)
    {
        std::size_t total = 0;
        while (!out.empty()) {
            if (m_pos == m_size) {
                m_size = m_r.read_some(m_buffer);
                m_pos = 0;
                if (m_size == 0) break; // end of stream
            }
            auto n = std::min(out.size(), m_size - m_pos);
            std::memcpy(out.data(), m_buffer.data() + m_pos, n);
            out = out.subspan(n);
            m_pos += n;
            total += n;
        }
        return total;
    }

private:
    R& m_r;
    std::array<std::byte, S> m_buffer;
    std::size_t m_pos = 0;
    std::size_t m_size = 0;
};

// Bridges the "partial reads are normal" Reader contract to the "I need
// exactly N bytes" needs of decoders. Malformed/truncated input is not an
// exception; it is a sticky failure flag. Once failed, every read is a no-op,
// so decoders can be written straight-line and checked at the end.
template <Reader R>
class decoder_context
{
public:
    explicit decoder_context(R& r) : m_r{r} {}

    bool good() const { return !m_failed; }
    void fail() { m_failed = true; }

    void read(std::span<std::byte> out)
    {
        while (!out.empty() && good()) {
            auto n = m_r.read_some(out);
            if (n == 0) { // short read: truncated input
                fail();
                return;
            }
            out = out.subspan(n);
        }
    }

private:
    R& m_r;
    bool m_failed = false;
};

inline constexpr auto decode_u8 = [](auto& r, std::uint8_t& v) {
    std::byte b;
    r.read(std::span{&b, 1});
    v = static_cast<std::uint8_t>(b);
};

inline constexpr auto decode_u16 = [](auto& r, std::uint16_t& v) {
    std::array<std::byte, 2> b;
    r.read(b);
    v = ReadLE16(b.data());
};

inline constexpr auto decode_u32 = [](auto& r, std::uint32_t& v) {
    std::array<std::byte, 4> b;
    r.read(b);
    v = ReadLE32(b.data());
};

inline constexpr auto decode_u64 = [](auto& r, std::uint64_t& v) {
    std::array<std::byte, 8> b;
    r.read(b);
    v = ReadLE64(b.data());
};

inline constexpr auto decode_hash256 = [](auto& r, uint256& v) {
    r.read(std::as_writable_bytes(std::span{v.data(), v.size()}));
};

// CompactSize decoder. Rejects non-canonical (over-long) encodings and
// enforces the same MAX_SIZE bound as the legacy reader, so a hostile peer
// cannot smuggle in an out-of-range or ambiguous length.
inline constexpr auto decode_size = [](auto& r, std::size_t& size) {
    std::uint8_t tag;
    decode_u8(r, tag);
    if (!r.good()) return;

    if (tag < 253) {
        size = tag;
        return;
    } else if (tag == 253) {
        std::uint16_t x;
        decode_u16(r, x);
        if (x < 253) { r.fail(); return; }
        size = x;
    } else if (tag == 254) {
        std::uint32_t x;
        decode_u32(r, x);
        if (x < 0x1'0000u) { r.fail(); return; }
        size = x;
    } else {
        std::uint64_t x;
        decode_u64(r, x);
        if (x < 0x1'0000'0000ull) { r.fail(); return; }
        size = x;
    }

    if (size > MAX_SIZE) r.fail();
};

// Decodes a length-prefixed sequence. Crucially it does NOT allocate `size`
// elements up front: an attacker-controlled length must not let a tiny message
// trigger a huge allocation. Capacity grows in bounded batches, so memory use
// tracks the bytes actually delivered.
inline constexpr auto decode_range = [](auto& r, auto& range, auto decode_elem) {
    std::size_t size = 0;
    decode_size(r, size);
    if (!r.good()) return;

    using elem_t = std::ranges::range_value_t<std::remove_reference_t<decltype(range)>>;
    range.clear();
    while (range.size() < size && r.good()) {
        if (range.size() == range.capacity()) {
            constexpr std::size_t batch = MAX_VECTOR_ALLOCATE / sizeof(elem_t);
            range.reserve(std::min(size, range.size() + std::max<std::size_t>(batch, 1)));
        }
        decode_elem(r, range.emplace_back());
    }
    if (!r.good()) range.clear();
};

inline constexpr auto decode_script = [](auto& r, CScript& out) {
    std::size_t size = 0;
    decode_size(r, size);
    if (!r.good()) return;

    // Grow incrementally, same anti-DoS reasoning as decode_range.
    std::vector<unsigned char> buf;
    while (buf.size() < size && r.good()) {
        if (buf.size() == buf.capacity()) {
            buf.reserve(std::min<std::size_t>(size, buf.size() + MAX_VECTOR_ALLOCATE));
        }
        std::byte b;
        r.read(std::span{&b, 1});
        buf.push_back(static_cast<unsigned char>(b));
    }
    if (!r.good()) return;
    out = CScript{buf.begin(), buf.end()};
};

inline constexpr auto decode_bytes = [](auto& r, std::vector<unsigned char>& out) {
    std::size_t size = 0;
    decode_size(r, size);
    if (!r.good()) return;

    out.clear();
    while (out.size() < size && r.good()) {
        if (out.size() == out.capacity()) {
            out.reserve(std::min<std::size_t>(size, out.size() + MAX_VECTOR_ALLOCATE));
        }
        std::byte b;
        r.read(std::span{&b, 1});
        out.push_back(static_cast<unsigned char>(b));
    }
    if (!r.good()) out.clear();
};

inline constexpr auto decode_outpoint = [](auto& r, COutPoint& out) {
    uint256 hash;
    decode_hash256(r, hash);
    out.hash = Txid::FromUint256(hash);
    decode_u32(r, out.n);
};

inline constexpr auto decode_txin = [](auto& r, CTxIn& in) {
    decode_outpoint(r, in.prevout);
    decode_script(r, in.scriptSig);
    decode_u32(r, in.nSequence);
};

inline constexpr auto decode_txout = [](auto& r, CTxOut& out) {
    std::uint64_t value;
    decode_u64(r, value);
    out.nValue = static_cast<CAmount>(value);
    decode_script(r, out.scriptPubKey);
};

inline constexpr auto decode_header = [](auto& r, CBlockHeader& h) {
    std::uint32_t v;
    decode_u32(r, v);
    h.nVersion = static_cast<std::int32_t>(v);
    decode_hash256(r, h.hashPrevBlock);
    decode_hash256(r, h.hashMerkleRoot);
    decode_u32(r, h.nTime);
    decode_u32(r, h.nBits);
    decode_u32(r, h.nNonce);
};

// Mirror of encode_tx, handling the optional witness marker. Follows the same
// "empty vin => maybe a witness-flagged tx" disambiguation as the legacy
// format, and rejects superfluous/empty witness records.
inline constexpr auto decode_tx = [](witness wmode) {
    return [=](auto& r, CMutableTransaction& tx) {
        decode_u32(r, tx.version);
        decode_range(r, tx.vin, decode_txin);

        bool with_witness = false;
        if (tx.vin.empty() && wmode == witness::allow && r.good()) {
            // Could be the extended format: the empty vin we just read was the
            // dummy. Read the flags byte.
            std::uint8_t flags;
            decode_u8(r, flags);
            if (flags == 1) {
                with_witness = true;
                decode_range(r, tx.vin, decode_txin);
                decode_range(r, tx.vout, decode_txout);
            } else if (flags == 0) {
                tx.vout.clear();
            } else {
                r.fail();
            }
        } else {
            decode_range(r, tx.vout, decode_txout);
        }

        if (with_witness) {
            for (auto& in : tx.vin) {
                decode_range(r, in.scriptWitness.stack, decode_bytes);
            }
            if (!tx.HasWitness()) r.fail(); // superfluous all-empty witness
        }

        decode_u32(r, tx.nLockTime);
    };
};

} // namespace

// -- Entry point definitions ------------------------------------------------

void serialize(const CBlockHeader& header, writer_ref writer)
{
    BufferedWriter<writer_ref> w{writer};
    encode_header(w, header);
}

void serialize(const CTransaction& tx, writer_ref writer, witness wmode)
{
    BufferedWriter<writer_ref> w{writer};
    encode_tx(wmode)(w, tx);
}

void serialize(const CBlock& block, writer_ref writer, witness wmode)
{
    BufferedWriter<writer_ref> w{writer};
    encode_block(wmode)(w, block);
}

std::optional<CBlockHeader> parse_block_header(reader_ref reader)
{
    buffered_reader<reader_ref> br{reader};
    decoder_context<buffered_reader<reader_ref>> ctx{br};

    CBlockHeader header;
    decode_header(ctx, header);
    if (!ctx.good()) return std::nullopt;
    return header;
}

std::optional<CMutableTransaction> parse_transaction(reader_ref reader, witness wmode)
{
    buffered_reader<reader_ref> br{reader};
    decoder_context<buffered_reader<reader_ref>> ctx{br};

    CMutableTransaction tx;
    decode_tx(wmode)(ctx, tx);
    if (!ctx.good()) return std::nullopt;
    return tx;
}

std::optional<CBlock> parse_block(reader_ref reader, witness wmode)
{
    buffered_reader<reader_ref> br{reader};
    decoder_context<buffered_reader<reader_ref>> ctx{br};

    CBlock block;
    decode_header(ctx, block);

    std::size_t count = 0;
    decode_size(ctx, count);
    if (!ctx.good()) return std::nullopt;

    auto decode_one = decode_tx(wmode);
    block.vtx.clear();
    while (block.vtx.size() < count && ctx.good()) {
        CMutableTransaction mtx;
        decode_one(ctx, mtx);
        if (!ctx.good()) break;
        block.vtx.push_back(MakeTransactionRef(std::move(mtx)));
    }
    if (!ctx.good()) return std::nullopt;
    return block;
}

} // namespace serialization
