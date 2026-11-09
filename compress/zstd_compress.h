#pragma once

#include "compress.h"

#include <cstring>

#include <zstd.h>

namespace my_log {
    namespace compress {
        class ZstdCompress final : public Compression {
        public:
            ZstdCompress();

            ~ZstdCompress() override;

            ZstdCompress(const ZstdCompress&) = delete;
            ZstdCompress& operator=(const ZstdCompress&) = delete;


            size_t Compress(const void* input, size_t input_size, void* output, size_t output_size) override;

            std::string Decompress(const void* data, size_t size) override;

            void ResetStream() override;

            size_t CompressedBound(size_t input_size) override;

        private:
            void ResetUncompressStream_();
            
            ZSTD_CCtx* cctx_;
            ZSTD_DCtx* dctx_;
};


    }

}