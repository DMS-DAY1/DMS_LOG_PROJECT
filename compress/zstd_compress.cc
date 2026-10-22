#include "zstd_compress.h"

#include <vector>

namespace my_log {
    namespace compress {
        static bool IsZSTDCompressed(const void* input, size_t input_size) {
            if (!input) {
                return false;
            }

            if (input_size < 4) {
                return false;
            }

            const uint8_t kMagicNumberBigEndian[] = {0x28, 0xb5, 0x2f, 0xfd};
            const uint8_t kMagicNumberLittleEndian[] = {0xfd, 0x2f, 0xb5, 0x28};
            if (memcmp(input, kMagicNumberBigEndian, sizeof(kMagicNumberBigEndian)) == 0) {
                return true;
            }

            if (memcmp(input, kMagicNumberLittleEndian, sizeof(kMagicNumberLittleEndian)) == 0) {
                return true;
            }

            return false;
    }
        ZstdCompress::ZstdCompress() {
            cctx_ = ZSTD_createCCtx();
            if (cctx_) {
            ZSTD_CCtx_setParameter(cctx_, ZSTD_c_compressionLevel, 5);
            }
            dctx_ = ZSTD_createDCtx();
        }

        ZstdCompress::~ZstdCompress () {
            if (cctx_) {
                ZSTD_freeCCtx(cctx_);
            }

            if (dctx_) {
                ZSTD_freeDCtx(dctx_);
            } 
        }

        void ZstdCompress::ResetStream() {
            if (cctx_) {
                ZSTD_CCtx_reset(cctx_, ZSTD_reset_session_only);
            }
        }

        void ZstdCompress::ResetUncompressStream_() {
            if (dctx_) {
                ZSTD_DCtx_reset(dctx_, ZSTD_reset_session_only);
            }
        }

        size_t ZstdCompress::Compress(const void* data, size_t size, void* output, size_t output_size) {
            if (!data || size == 0) {
                return 0;
            }
            ZSTD_inBuffer input = {data, size, 0};
            ZSTD_outBuffer output_buffer = {const_cast<void*>(reinterpret_cast<const void*>(output)), output_size, 0};
            size_t ret = ZSTD_compressStream2(cctx_, &output_buffer, &input, ZSTD_e_flush);
            if (ZSTD_isError(ret) != 0) {
                return 0;
            }
            return output_buffer.pos;
        }

        std::string ZstdCompress::Decompress(const void* data, size_t size) {
            if (!data || size == 0) {
                return "";
            }
            if (IsZSTDCompressed(data, size)) {
                ResetUncompressStream_();
            }
            if (!dctx_) {
                return "";
            }
            ZSTD_inBuffer in_buf = {data, size, 0};
            std::string result;
            std::vector<char> temp_buf(4096);
            ZSTD_outBuffer out_buf = {temp_buf.data(), temp_buf.size(), 0};
            do {
                out_buf.pos = 0;
                size_t ret = ZSTD_decompressStream(dctx_, &out_buf, &in_buf);
                
                if (ZSTD_isError(ret)) {
                    return "";
                }
                result.append(temp_buf.data(), out_buf.pos);
            } while (out_buf.pos == temp_buf.size());
            return result;
        }

    size_t ZstdCompress::CompressedBound(size_t input_size) {
        return ZSTD_compressBound(input_size);
    }

    }
}