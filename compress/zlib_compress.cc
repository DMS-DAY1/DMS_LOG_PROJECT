#include "zlib_compress.h"

namespace my_log {
    namespace compress {
        static bool Iscompressed(const void* input, size_t input_size) {
            const uint8_t* p = static_cast<const uint8_t*>(input);
            return input_size >= 2
                && (p[0] & 0x0F) == 8                    // CM=8（deflate）
                && ((p[0] << 8) | p[1]) % 31 == 0        // 头校验：必须是 31 的倍数
                && !(p[1] & 0x20);                       // FDICT=0（无预设字典）
        }

        size_t ZlibCompress::Compress(const void* input, size_t input_size, void* output, size_t output_size) {
            if (!output || !input) {
                return 0;
            }

            if (!compress_stream_) {
                return 0;
            }

            compress_stream_->next_in = (Bytef*)input;
            compress_stream_->avail_in = static_cast<uInt>(input_size);

            compress_stream_->next_out = (Bytef*)output;
            compress_stream_->avail_out = static_cast<uInt>(output_size);

                int ret = deflate(compress_stream_.get(),Z_SYNC_FLUSH);
                if (ret != Z_OK && ret != Z_BUF_ERROR) {
                    return 0;
                }

            size_t outlen = output_size - compress_stream_->avail_out;

            return outlen;
        }

        std::string ZlibCompress::Decompress(const void* data, size_t size) {
            if (!data) {
                return "";
            }

            if (Iscompressed(data, size)) {
                ResetUncompressStream_();
            }

            if (!uncompress_stream_) {
                return "";
            }

            uncompress_stream_->next_in = (Bytef*)data;
            uncompress_stream_->avail_in = static_cast<uInt>(size);

            std::string output;
            while (uncompress_stream_->avail_in > 0) {
                char buffer[4096] = {0};
                uncompress_stream_->next_out = (Bytef*)buffer;
                uncompress_stream_->avail_out = (uInt)sizeof(buffer);
                int ret = inflate(uncompress_stream_.get(), Z_NO_FLUSH);
                
                if (ret != Z_OK && ret != Z_BUF_ERROR) {
                    return "";
                }

                output.append(buffer, sizeof(buffer) - uncompress_stream_->avail_out);
            }
            
            return output;
        }

        void ZlibCompress::ResetUncompressStream_() {
            uncompress_stream_ = std::unique_ptr<z_stream,ZStreamInflateDeleter>(new z_stream());
            uncompress_stream_->zalloc = Z_NULL;
            uncompress_stream_->zfree = Z_NULL;
            uncompress_stream_->opaque = Z_NULL;
            uncompress_stream_->avail_in = 0;
            uncompress_stream_->next_in = Z_NULL;

            int ret = inflateInit2(uncompress_stream_.get(), MAX_WBITS);

            if (ret != Z_OK) {
                uncompress_stream_.reset();
            }

        }

        void ZlibCompress::ResetStream() {
            compress_stream_ = std::unique_ptr<z_stream, ZStreamDeflateDeleter>(new z_stream());
            compress_stream_->zalloc = Z_NULL;
            compress_stream_->zfree = Z_NULL;
            compress_stream_->opaque = Z_NULL;

            int32_t ret = deflateInit2(compress_stream_.get(), Z_BEST_COMPRESSION, Z_DEFLATED, MAX_WBITS, MAX_MEM_LEVEL,
                             Z_DEFAULT_STRATEGY);
            if (ret != Z_OK) {
                compress_stream_.reset();
            }
        }

        size_t ZlibCompress::CompressedBound(size_t input_size) {
            return compressBound(input_size);
        }

    }  
}