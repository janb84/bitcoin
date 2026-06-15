// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SERIALIZATION_FORMAT_H
#define BITCOIN_SERIALIZATION_FORMAT_H

#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>

// Format-driven (de)serialization for consensus types.
//
// This module applies the design from
//   https://purplekarrot.net/blog/serialization.html
//   https://purplekarrot.net/blog/deserialization.html
//
// The guiding insight is that the *serialization format* is the source of
// truth, not the C++ class layout. Rather than threading templated
// operator<</>> overloads and SERIALIZE_METHODS macros through hundreds of
// translation units, the actual byte-level encoders and decoders live in a
// single .cpp behind a compilation boundary. Callers only ever see:
//
//   - the Writer / Reader concepts (minimal byte-stream contracts),
//   - the type-erased writer_ref / reader_ref adapters that let any model of
//     those concepts cross the boundary without templating the call site, and
//   - a handful of top-level entry points.
//
// Deserialization treats malformed/truncated input as routine (it is untrusted
// network data) and reports it via std::optional rather than exceptions.

class CBlock;
class CBlockHeader;
class CTransaction;
struct CMutableTransaction;

namespace serialization {

//! Minimal contract for a byte sink: it can accept a run of bytes.
template <typename T>
concept Writer = requires(T& w, std::span<const std::byte> bytes) {
    { w.write(bytes) };
};

//! Minimal contract for a byte source. read_some() returns [0, out.size()]
//! bytes; returning fewer than requested is normal and 0 means end-of-stream.
template <typename T>
concept Reader = requires(T& r, std::span<std::byte> out) {
    { r.read_some(out) } -> std::same_as<std::size_t>;
};

//! Type-erased reference to any Writer. This is the compilation boundary: the
//! encoders are compiled once against writer_ref, so adding a new byte sink
//! does not recompile (or even see) the encoder bodies.
class writer_ref
{
public:
    template <Writer W>
        requires(!std::same_as<std::remove_cvref_t<W>, writer_ref>)
    writer_ref(W& object)
        : m_ptr{std::addressof(object)},
          m_write{[](void* p, std::span<const std::byte> bytes) {
              static_cast<W*>(p)->write(bytes);
          }}
    {
    }

    void write(std::span<const std::byte> bytes) { m_write(m_ptr, bytes); }

private:
    void* m_ptr;
    void (*m_write)(void*, std::span<const std::byte>);
};

//! Type-erased reference to any Reader, mirroring writer_ref.
class reader_ref
{
public:
    template <Reader R>
        requires(!std::same_as<std::remove_cvref_t<R>, reader_ref>)
    reader_ref(R& object)
        : m_ptr{std::addressof(object)},
          m_read_some{[](void* p, std::span<std::byte> out) {
              return static_cast<R*>(p)->read_some(out);
          }}
    {
    }

    std::size_t read_some(std::span<std::byte> out) { return m_read_some(m_ptr, out); }

private:
    void* m_ptr;
    std::size_t (*m_read_some)(void*, std::span<std::byte>);
};

//! Witness handling policy, parameterising the transaction/block format.
enum class witness { disallow, allow };

// -- Top-level entry points -------------------------------------------------

void serialize(const CBlockHeader& header, writer_ref writer);
void serialize(const CTransaction& tx, writer_ref writer, witness wmode = witness::allow);
void serialize(const CBlock& block, writer_ref writer, witness wmode = witness::allow);

std::optional<CBlockHeader> parse_block_header(reader_ref reader);
std::optional<CMutableTransaction> parse_transaction(reader_ref reader, witness wmode = witness::allow);
std::optional<CBlock> parse_block(reader_ref reader, witness wmode = witness::allow);

} // namespace serialization

#endif // BITCOIN_SERIALIZATION_FORMAT_H
