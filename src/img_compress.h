#ifndef IMG_COMPRESS_H
#define IMG_COMPRESS_H

#include <stdint.h>
#include <string.h>

// =================================================================================
// DEFINIÇÕES
// =================================================================================

// Modos de compressão disponíveis
typedef enum {
    IMG_COMPRESS_NONE = 0,      // Sem compressão
    IMG_COMPRESS_RLE = 1,       // Run-Length Encoding
    IMG_COMPRESS_BLOCK4 = 2,    // Compressão por blocos 4x4
    IMG_COMPRESS_DOWN2 = 3,     // Downsample 2:1 + RLE
    IMG_COMPRESS_DICT = 4       // Compressão por dicionário (palette)
} ImgCompressMode;

// Estrutura para imagem comprimida
typedef struct {
    uint8_t* data;
    uint16_t size;
    uint8_t mode;
    uint8_t original_width;
    uint8_t original_height;
} CompressedImage;

// =================================================================================
// IMPLEMENTAÇÃO COMPLETA INLINE
// =================================================================================

// Buffers estáticos com nomes ÚNICOS para evitar conflitos
static uint8_t _img_compress_buffer[1024];      // Buffer para imagem comprimida
static uint8_t _img_temp_buffer[1024];          // Buffer temporário

// ---------------------------------------------------------------------------------
// Funções internas
// ---------------------------------------------------------------------------------

/**
 * @brief Compressão RLE (Run-Length Encoding)
 */
static inline uint16_t _img_compress_rle(const uint8_t* input, uint16_t length, uint8_t* output) {
    uint16_t out_idx = 0;
    uint16_t in_idx = 0;
    
    while (in_idx < length) {
        uint8_t current = input[in_idx];
        uint8_t count = 1;
        
        // Conta pixels consecutivos iguais (máx 255)
        while (in_idx + count < length && 
               input[in_idx + count] == current && 
               count < 255) {
            count++;
        }
        
        output[out_idx++] = count;
        output[out_idx++] = current;
        in_idx += count;
    }
    
    return out_idx;
}

/**
 * @brief Descompressão RLE
 */
static inline uint16_t _img_decompress_rle(const uint8_t* input, uint16_t length, uint8_t* output, uint16_t max_out) {
    uint16_t out_idx = 0;
    uint16_t in_idx = 0;
    
    while (in_idx < length && out_idx < max_out) {
        if (in_idx + 1 >= length) break;
        
        uint8_t count = input[in_idx++];
        uint8_t value = input[in_idx++];
        
        // Preenche com o valor repetido
        memset(output + out_idx, value, count);
        out_idx += count;
    }
    
    return out_idx;
}

/**
 * @brief Compressão por blocos 4x4 (versão simplificada)
 */
static inline uint16_t _img_compress_block4(const uint8_t* pixels, uint8_t width, uint8_t height, uint8_t* output) {
    uint16_t out_idx = 0;
    
    // Cada bloco 4x4 = 16 pixels -> compactação simplificada
    for (uint8_t y = 0; y < height; y += 4) {
        for (uint8_t x = 0; x < width; x += 4) {
            uint8_t min_val = 255;
            uint8_t max_val = 0;
            uint16_t sum = 0;
            uint8_t count = 0;
            
            // Encontra min, max e média do bloco
            for (uint8_t dy = 0; dy < 4 && y + dy < height; dy++) {
                for (uint8_t dx = 0; dx < 4 && x + dx < width; dx++) {
                    uint8_t val = pixels[(y + dy) * width + (x + dx)];
                    sum += val;
                    count++;
                    if (val < min_val) min_val = val;
                    if (val > max_val) max_val = val;
                }
            }
            
            uint8_t avg = count > 0 ? sum / count : 0;
            uint8_t range = max_val - min_val;
            
            // Se bloco uniforme (baixa variação)
            if (range <= 32) {
                // Armazena média e range
                output[out_idx++] = avg;
                output[out_idx++] = range;
                
                // Para cada pixel, armazena delta de 2 bits (4 níveis)
                uint8_t delta_byte = 0;
                uint8_t bit_pos = 0;
                
                for (uint8_t dy = 0; dy < 4 && y + dy < height; dy++) {
                    for (uint8_t dx = 0; dx < 4 && x + dx < width; dx++) {
                        uint8_t val = pixels[(y + dy) * width + (x + dx)];
                        uint8_t delta_level = 0;
                        
                        if (range > 0) {
                            float norm = (float)(val - min_val) / range;
                            delta_level = (uint8_t)(norm * 3); // 0-3
                        }
                        
                        if (bit_pos == 0) {
                            delta_byte = delta_level;
                            bit_pos = 2;
                        } else if (bit_pos == 2) {
                            delta_byte |= delta_level << 2;
                            bit_pos = 4;
                        } else if (bit_pos == 4) {
                            delta_byte |= delta_level << 4;
                            bit_pos = 6;
                        } else {
                            delta_byte |= delta_level << 6;
                            output[out_idx++] = delta_byte;
                            bit_pos = 0;
                        }
                    }
                }
                
                // Se sobrou bits não escritos
                if (bit_pos != 0) {
                    output[out_idx++] = delta_byte;
                }
            } else {
                // Bloco complexo - armazena 4 pixels representativos
                output[out_idx++] = min_val;
                output[out_idx++] = max_val;
                
                // Canto superior esquerdo
                if (y < height && x < width) 
                    output[out_idx++] = pixels[y * width + x];
                // Canto superior direito
                if (y < height && x + 3 < width) 
                    output[out_idx++] = pixels[y * width + (x + 3)];
                // Canto inferior esquerdo
                if (y + 3 < height && x < width) 
                    output[out_idx++] = pixels[(y + 3) * width + x];
                // Canto inferior direito
                if (y + 3 < height && x + 3 < width) 
                    output[out_idx++] = pixels[(y + 3) * width + (x + 3)];
            }
        }
    }
    
    return out_idx;
}

/**
 * @brief Descompressão por blocos 4x4
 */
static inline uint16_t _img_decompress_block4(const uint8_t* input, uint16_t length, uint8_t* output, uint8_t width, uint8_t height) {
    uint16_t in_idx = 0;
    
    for (uint8_t y = 0; y < height; y += 4) {
        for (uint8_t x = 0; x < width; x += 4) {
            if (in_idx + 2 > length) break;
            
            uint8_t val1 = input[in_idx++];
            uint8_t val2 = input[in_idx++];
            
            // O compressor usa o segundo byte para o 'range' no modo uniforme (<= 32)
            // ou para o 'max_val' no modo complexo (> 32)
            if (val2 <= 32) {
                // MODO UNIFORME: Decodifica pixels de 2 bits
                uint8_t avg = val1;
                uint8_t range = val2;
                uint8_t min_val = (avg > range / 2) ? (avg - range / 2) : 0;
                
                uint8_t delta_byte = 0;
                uint8_t bit_pos = 0;
                
                for (uint8_t dy = 0; dy < 4 && y + dy < height; dy++) {
                    for (uint8_t dx = 0; dx < 4 && x + dx < width; dx++) {
                        if (bit_pos == 0) {
                            delta_byte = input[in_idx++];
                        }
                        
                        uint8_t delta_level = (delta_byte >> bit_pos) & 0x03;
                        bit_pos = (bit_pos + 2) % 8;
                        
                        // Reconstrói o valor aproximado: min + (nível * range / 3)
                        uint8_t final_val = min_val + (delta_level * range / 3);
                        output[(y + dy) * width + (x + dx)] = final_val;
                    }
                }
            } else {
                // MODO COMPLEXO: Recupera os 4 cantos
                uint8_t min_val = val1;
                uint8_t max_val = val2;
                uint8_t mid_val = (min_val + max_val) / 2;
                
                uint8_t tl = input[in_idx++]; // Top-Left
                uint8_t tr = input[in_idx++]; // Top-Right
                uint8_t bl = input[in_idx++]; // Bottom-Left
                uint8_t br = input[in_idx++]; // Bottom-Right
                
                for (uint8_t dy = 0; dy < 4 && y + dy < height; dy++) {
                    for (uint8_t dx = 0; dx < 4 && x + dx < width; dx++) {
                        uint8_t final_val;
                        // Distribuição simplificada baseada nos cantos
                        if (dy < 2) {
                            final_val = (dx < 2) ? tl : tr;
                        } else {
                            final_val = (dx < 2) ? bl : br;
                        }
                        output[(y + dy) * width + (x + dx)] = final_val;
                    }
                }
            }
        }
    }
    return width * height;
}

/**
 * @brief Downsample 2:1 + RLE
 */
static inline uint16_t _img_compress_downsample2(const uint8_t* pixels, uint8_t width, uint8_t height, uint8_t* output) {
    uint8_t small_w = (width + 1) / 2;
    uint8_t small_h = (height + 1) / 2;
    
    // Calcula tamanho da imagem reduzida
    uint16_t small_size = small_w * small_h;
    if (small_size > 128) {
        // Limita para 128 bytes (16x16)
        small_w = 16;
        small_h = small_size > 256 ? 16 : small_h;
        small_size = small_w * small_h;
    }
    
    // Downsample: média de 4 pixels
    for (uint8_t y = 0; y < small_h; y++) {
        for (uint8_t x = 0; x < small_w; x++) {
            uint16_t sum = 0;
            uint8_t count = 0;
            
            for (uint8_t dy = 0; dy < 2; dy++) {
                for (uint8_t dx = 0; dx < 2; dx++) {
                    uint8_t px = y * 2 + dy;
                    uint8_t py = x * 2 + dx;
                    if (px < height && py < width) {
                        sum += pixels[px * width + py];
                        count++;
                    }
                }
            }
            
            _img_temp_buffer[y * small_w + x] = count > 0 ? (sum / count) : 0;
        }
    }
    
    // Aplica RLE na imagem reduzida
    return _img_compress_rle(_img_temp_buffer, small_size, output);
}

/**
 * @brief Descompressão Downsample 2:1 + RLE
 */
static inline uint16_t _img_decompress_downsample2(const uint8_t* input, uint16_t length, uint8_t* output, uint8_t width, uint8_t height) {
    uint8_t small_w = (width + 1) / 2;
    uint8_t small_h = (height + 1) / 2;
    uint16_t small_size = small_w * small_h;

    // Recria exatamente a lógica de limitação de 16x16 usada no compressor
    if (small_size > 128) {
        small_w = 16;
        small_h = small_size > 256 ? 16 : small_h;
        small_size = small_w * small_h;
    }

    // 1. Primeiro, recuperamos a imagem reduzida do RLE para o buffer temporário
    if (_img_decompress_rle(input, length, _img_temp_buffer, sizeof(_img_temp_buffer)) != small_size) {
        return 0;
    }

    // 2. Upsampling: Expandimos os pixels reduzidos para o tamanho original
    for (uint8_t y = 0; y < height; y++) {
        for (uint8_t x = 0; x < width; x++) {
            // Mapeia as coordenadas originais para as coordenadas reduzidas (x/2, y/2)
            uint8_t sx = x / 2;
            uint8_t sy = y / 2;
            
            // Proteção contra o limite de 16x16 imposto na compressão
            if (sx >= small_w) sx = small_w - 1;
            if (sy >= small_h) sy = small_h - 1;
            
            output[y * width + x] = _img_temp_buffer[sy * small_w + sx];
        }
    }

    return width * height;
}

/**
 * @brief Compressão por dicionário (palette de 16 cores)
 */
static inline uint16_t _img_compress_dict(const uint8_t* pixels, uint16_t length, uint8_t* output) {
    // Cria uma paleta simples de 16 cores
    uint8_t palette[16] = {0};
    
    // Preenche paleta com valores espaçados
    for (uint8_t i = 0; i < 16; i++) {
        palette[i] = i * 16;
    }
    
    // Primeiros bytes: tamanho da paleta e paleta
    output[0] = 16; // Tamanho da paleta
    memcpy(output + 1, palette, 16);
    
    uint16_t out_idx = 17;
    
    // Codifica imagem: 2 pixels por byte (4 bits cada)
    for (uint16_t i = 0; i < length; i += 2) {
        uint8_t pixel1 = pixels[i] / 16;      // Converte para 0-15
        uint8_t pixel2 = (i + 1 < length) ? (pixels[i + 1] / 16) : 0;
        output[out_idx++] = (pixel1 << 4) | pixel2;
    }
    
    return out_idx;
}

/**
 * @brief Descompressão por dicionário (palette de 16 cores)
 */
static inline uint16_t _img_decompress_dict(const uint8_t* input, uint16_t length, uint8_t* output, uint16_t max_out) {
    if (length < 17) return 0; // Precisa de pelo menos o tamanho da paleta + paleta

    uint8_t palette_size = input[0];
    const uint8_t* palette = &input[1];
    const uint8_t* compressed_data = &input[1 + palette_size];
    
    uint16_t out_idx = 0;
    uint16_t in_idx = 0;
    uint16_t data_len = length - 1 - palette_size;

    while (in_idx < data_len && out_idx < max_out) {
        uint8_t packed = compressed_data[in_idx++];
        
        // Extrai o primeiro pixel (bits 4-7)
        uint8_t idx1 = (packed >> 4) & 0x0F;
        output[out_idx++] = palette[idx1];
        
        // Extrai o segundo pixel (bits 0-3), se houver espaço
        if (out_idx < max_out) {
            uint8_t idx2 = packed & 0x0F;
            output[out_idx++] = palette[idx2];
        }
    }
    
    return out_idx;
}

// ---------------------------------------------------------------------------------
// Funções públicas
// ---------------------------------------------------------------------------------

/**
 * @brief Comprime uma imagem (8-bit grayscale)
 */
static inline CompressedImage img_compress(const uint8_t* pixels, uint8_t width, uint8_t height, ImgCompressMode mode) {
    CompressedImage result = {0};
    uint16_t original_size = width * height;
    
    if (original_size == 0) {
        result.data = _img_compress_buffer;
        result.size = 0;
        return result;
    }
    
    // Header: width | height | mode
    _img_compress_buffer[0] = width;
    _img_compress_buffer[1] = height;
    _img_compress_buffer[2] = (uint8_t)mode;
    
    uint16_t data_start = 3;
    uint16_t compressed_size = 0;
    
    switch (mode) {
        case IMG_COMPRESS_NONE:
            // Sem compressão - copia direto
            if (original_size <= sizeof(_img_compress_buffer) - data_start) {
                memcpy(_img_compress_buffer + data_start, pixels, original_size);
                compressed_size = original_size;
            }
            break;
            
        case IMG_COMPRESS_RLE:
            compressed_size = _img_compress_rle(pixels, original_size, _img_compress_buffer + data_start);
            break;
            
        case IMG_COMPRESS_BLOCK4:
            compressed_size = _img_compress_block4(pixels, width, height, _img_compress_buffer + data_start);
            break;
            
        case IMG_COMPRESS_DOWN2:
            compressed_size = _img_compress_downsample2(pixels, width, height, _img_compress_buffer + data_start);
            break;
            
        case IMG_COMPRESS_DICT:
            compressed_size = _img_compress_dict(pixels, original_size, _img_compress_buffer + data_start);
            break;
            
        default:
            // Fallback para sem compressão
            if (original_size <= sizeof(_img_compress_buffer) - data_start) {
                memcpy(_img_compress_buffer + data_start, pixels, original_size);
                compressed_size = original_size;
                _img_compress_buffer[2] = IMG_COMPRESS_NONE;
            }
            break;
    }
    
    // Se falhou, usa sem compressão
    if (compressed_size == 0) {
        if (original_size <= sizeof(_img_compress_buffer) - data_start) {
            memcpy(_img_compress_buffer + data_start, pixels, original_size);
            compressed_size = original_size;
            _img_compress_buffer[2] = IMG_COMPRESS_NONE;
        }
    }
    
    result.data = _img_compress_buffer;
    result.size = data_start + compressed_size;
    result.mode = _img_compress_buffer[2];
    result.original_width = width;
    result.original_height = height;
    
    return result;
}

/**
 * @brief Descomprime uma imagem
 */
static inline int img_decompress(const CompressedImage* compressed, uint8_t* output) {
    if (!compressed || !compressed->data || compressed->size < 3) {
        return 0;
    }
    
    uint8_t width = compressed->data[0];
    uint8_t height = compressed->data[1];
    uint8_t mode = compressed->data[2];
    const uint8_t* data = compressed->data + 3;
    uint16_t data_size = compressed->size - 3;
    uint16_t original_size = width * height;
    
    if (original_size == 0) return 0;
    
    switch (mode) {
        case IMG_COMPRESS_NONE:
            if (data_size >= original_size) {
                memcpy(output, data, original_size);
                return 1;
            }
            break;
            
        case IMG_COMPRESS_RLE:
            if (_img_decompress_rle(data, data_size, output, original_size) == original_size) {
                return 1;
            }
            break;
            
        case IMG_COMPRESS_DICT:
            if (_img_decompress_dict(data, data_size, output, original_size) == original_size) {
                return 1;
            }
            break;

        case IMG_COMPRESS_DOWN2:
            if (_img_decompress_downsample2(data, data_size, output, width, height) == original_size) {
                return 1;
            }
            break;

        case IMG_COMPRESS_BLOCK4:
            if (_img_decompress_block4(data, data_size, output, width, height) == original_size) {
                return 1;
            }
            break;
    }
    
    return 0;
}

/**
 * @brief Calcula taxa de compressão
 */
static inline float img_compression_ratio(uint16_t original_size, const CompressedImage* compressed) {
    if (original_size == 0) return 0.0f;
    return 1.0f - ((float)compressed->size / original_size);
}

/**
 * @brief Função auxiliar para criar imagem de teste
 */
static inline void img_create_test_pattern(uint8_t* buffer, uint8_t width, uint8_t height) {
    for (uint8_t y = 0; y < height; y++) {
        for (uint8_t x = 0; x < width; x++) {
            // Padrão de grades
            if ((x / 4 + y / 4) % 2 == 0) {
                buffer[y * width + x] = 255;
            } else {
                buffer[y * width + x] = 0;
            }
        }
    }
}

#endif // IMG_COMPRESS_H