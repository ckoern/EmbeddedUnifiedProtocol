#include "eup/cobs.hpp"

namespace eup {

CobsResult cobs_encode(const std::uint8_t* src, std::size_t srcLen,
                       std::uint8_t* dst, std::size_t dstCap) noexcept {
    // The code byte for the current block is held back until the block is
    // closed (by a 0x00 in the input, a full 254-byte run, or end of input).
    std::size_t write = 1;        // dst[0] is reserved for the first code byte
    std::size_t codeIdx = 0;      // index of the pending code byte
    std::uint8_t code = 1;        // 1 + number of non-zero bytes in this block

    if (dstCap < 1) {
        return {CobsStatus::OutputTooSmall, 0};
    }

    for (std::size_t read = 0; read < srcLen; ++read) {
        if (src[read] == 0) {
            // Close the current block; the 0x00 is encoded implicitly.
            dst[codeIdx] = code;
            if (write >= dstCap) {
                return {CobsStatus::OutputTooSmall, 0};
            }
            codeIdx = write++;
            code = 1;
        } else {
            if (write >= dstCap) {
                return {CobsStatus::OutputTooSmall, 0};
            }
            dst[write++] = src[read];
            ++code;
            if (code == 0xFF) {
                // Maximal block reached; emit it.
                dst[codeIdx] = code;
                // Only start a fresh block if there are more input bytes.
                // If this was the last byte, no trailing code byte is needed.
                if (read + 1 == srcLen) {
                    return {CobsStatus::Ok, write};
                }
                if (write >= dstCap) {
                    return {CobsStatus::OutputTooSmall, 0};
                }
                codeIdx = write++;
                code = 1;
            }
        }
    }

    // Close the final (possibly empty) block.
    dst[codeIdx] = code;
    return {CobsStatus::Ok, write};
}

CobsResult cobs_decode(const std::uint8_t* src, std::size_t srcLen,
                       std::uint8_t* dst, std::size_t dstCap) noexcept {
    std::size_t read = 0;
    std::size_t write = 0;

    while (read < srcLen) {
        const std::uint8_t code = src[read++];
        if (code == 0) {
            // A delimiter must never appear inside a COBS block.
            return {CobsStatus::MalformedInput, 0};
        }

        for (std::uint8_t i = 1; i < code; ++i) {
            if (read >= srcLen) {
                return {CobsStatus::MalformedInput, 0};
            }
            if (write >= dstCap) {
                return {CobsStatus::OutputTooSmall, 0};
            }
            dst[write++] = src[read++];
        }

        // A code < 0xFF that is not the last block represents a trailing 0x00.
        if (code != 0xFF && read < srcLen) {
            if (write >= dstCap) {
                return {CobsStatus::OutputTooSmall, 0};
            }
            dst[write++] = 0;
        }
    }

    return {CobsStatus::Ok, write};
}

}  // namespace eup
