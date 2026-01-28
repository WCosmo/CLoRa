#ifndef COSMIC_PAYLOAD_H
#define COSMIC_PAYLOAD_H

#include <Arduino.h>
#include "fastlz.h" 
#include "mini_aes.h" 
#include "img_compress.h"  // Nova biblioteca de compressão de imagem

// =================================================================================
// CONFIGURAÇÕES
// =================================================================================

// Tamanhos dos buffers
#define MAX_COSMIC_BUFFER 512          // Aumentado para suportar imagens
#define HEADER_SIZE 4                  // Tamanho do cabeçalho (não alterar)
#define MAX_IMAGE_SIZE 256             // Máx 256 pixels (ex: 16x16)

// Tipos de pacote
#define PKG_TYPE_TELEMETRY 0x10        // Telemetria (floats)
#define PKG_TYPE_IMAGE     0x20        // Imagem (8-bit grayscale)
#define PKG_TYPE_COMMAND   0x30        // Comandos/controle
#define PKG_TYPE_STATUS    0x40        // Status do dispositivo

// Modos de compressão
#define COMPRESS_NONE      0x00        // Sem compressão
#define COMPRESS_COSMIC    0x01        // Compressão COSMIC (floats)
#define COMPRESS_IMG_RLE   0x02        // Imagem: Run-Length Encoding
#define COMPRESS_IMG_BLOCK 0x03        // Imagem: Compressão por blocos
#define COMPRESS_IMG_DOWN2 0x04        // Imagem: Downsample 2:1

// =================================================================================
// BUFFERS INTERNOS
// =================================================================================

// Buffers para processamento geral
static uint8_t _c_buffer[MAX_COSMIC_BUFFER];      // Buffer Final (Header + Payload)
static uint8_t _work_buffer[MAX_COSMIC_BUFFER];   // Buffer Temporário de Trabalho

// Buffer para conversão int16 (usado na compressão COSMIC)
static int16_t _raw_int_buffer[MAX_COSMIC_BUFFER / 2]; 

// Buffer para processamento de imagem
static uint8_t _img_buffer[MAX_IMAGE_SIZE];       // Buffer para imagens

// =================================================================================
// ESTADO DA CRIPTOGRAFIA
// =================================================================================

static uint8_t _cosmic_key[16] = {0};
static bool _encryption_enabled = false;
static uint32_t _packet_counter = 0;              // Contador de pacotes para IV único

// =================================================================================
// ESTRUTURAS DE DADOS
// =================================================================================

/**
 * @brief Pacote COSMIC genérico
 */
struct CosmicPacket {
    uint8_t* data;
    uint8_t size;
    uint8_t type;
    uint8_t mode;
};

/**
 * @brief Pacote de imagem especializado
 */
struct CosmicImagePacket {
    uint8_t* data;
    uint8_t size;
    uint8_t img_width;
    uint8_t img_height;
    uint8_t compress_mode;
};

// =================================================================================
// FUNÇÕES INTERNAS
// =================================================================================

/**
 * @brief Prepara o cabeçalho do pacote
 */
static void _prepare_header(uint8_t net_id, uint8_t dev_id, uint8_t pkg_type, uint8_t mod) {
    _c_buffer[0] = net_id;
    _c_buffer[1] = dev_id;
    _c_buffer[2] = pkg_type;
    _c_buffer[3] = mod;
}

/**
 * @brief Prepara vetor de inicialização (IV) para criptografia
 * @param iv Buffer de 16 bytes para o IV
 * @param net_id Network ID
 * @param counter Contador de pacotes (evita reutilização)
 */
static void _prepare_iv(uint8_t iv[16], uint8_t net_id, uint32_t counter) {
    memset(iv, 0, 16);
    iv[0] = net_id;                    // Byte 0: Network ID
    
    // Bytes 12-15: Contador de pacotes (big-endian)
    iv[12] = (counter >> 24) & 0xFF;
    iv[13] = (counter >> 16) & 0xFF;
    iv[14] = (counter >> 8) & 0xFF;
    iv[15] = counter & 0xFF;
    
    _packet_counter++;                 // Incrementa para próximo pacote
}

/**
 * @brief Aplica criptografia no pacote
 * @param buffer Pacote a ser cifrado
 * @param size Tamanho do pacote
 * @param net_id Network ID para gerar IV
 * @return 1 se criptografado, 0 se não
 */
static int _apply_encryption(uint8_t* buffer, uint8_t size, uint8_t net_id) {
    if (!_encryption_enabled) return 0;
    
    uint8_t iv[16];
    _prepare_iv(iv, net_id, _packet_counter);
    maes_ctr_process(buffer, size, iv, _cosmic_key);
    return 1;
}

// =================================================================================
// API PÚBLICA - GERAL
// =================================================================================

/**
 * @brief Configura chave de criptografia
 * @param key Chave de 16 bytes
 */
void setCosmicKey(const uint8_t key[16]) {
    memcpy(_cosmic_key, key, 16);
    _encryption_enabled = true;
    _packet_counter = 0;  // Reinicia contador ao mudar chave
}

/**
 * @brief Desabilita criptografia
 */
void disableEncryption() {
    _encryption_enabled = false;
}

/**
 * @brief Habilita criptografia
 */
void enableEncryption() {
    _encryption_enabled = true;
}

/**
 * @brief Retorna estado da criptografia
 * @return true se habilitada, false se não
 */
bool isEncryptionEnabled() {
    return _encryption_enabled;
}

// =================================================================================
// API PÚBLICA - TELEMETRIA (FUNÇÕES ORIGINAIS)
// =================================================================================

/**
 * @brief ppkg (Pack of Floats) - Função original
 * @param compress true para compressão COSMIC, false para raw
 * @param nid Network ID
 * @param did Device ID
 * @param type Tipo de pacote (usar PKG_TYPE_TELEMETRY)
 * @param mod Modo de compressão (0-4)
 * @param pack Array de floats
 * @param n Número de floats no array
 * @return Pacote pronto para transmissão
 */
CosmicPacket ppkg(bool compress, uint8_t nid, uint8_t did, uint8_t type, uint8_t mod, float* pack, int n) {
    // 1. Escreve Cabeçalho no buffer final
    _prepare_header(nid, did, type, mod);

    int max_floats = (MAX_COSMIC_BUFFER - HEADER_SIZE) / (compress ? 2 : 4);
    if (n > max_floats) n = max_floats;
    
    // Tratamento para pacote vazio (apenas Header)
    if (n <= 0) {
        CosmicPacket pkg; 
        pkg.data = _c_buffer; 
        pkg.size = HEADER_SIZE;
        pkg.type = type;
        pkg.mode = mod;
        
        // Aplica criptografia se habilitada
        _apply_encryption(_c_buffer, HEADER_SIZE, nid);
        return pkg;
    }

    int payload_size = 0;

    // 2. Processamento dos Dados (Compressão ou Raw)
    if (!compress) {
        // --- MODO RAW (32-bit) ---
        payload_size = n * sizeof(float);
        memcpy(_work_buffer, pack, payload_size);
    } 
    else {
        // --- MODO COSMIC (Quant + Delta + LZ77) ---
        _raw_int_buffer[0] = (int16_t)(pack[0] * 100.0);
        for(int i = 1; i < n; i++) {
            _raw_int_buffer[i] = (int16_t)(pack[i] * 100.0) - (int16_t)(pack[i-1] * 100.0);
        }

        int raw_int_size = n * sizeof(int16_t);
        int lz_size = fastlz_compress_level(1, _raw_int_buffer, raw_int_size, _work_buffer);

        if (lz_size <= 0 || lz_size >= raw_int_size) {
            memcpy(_work_buffer, _raw_int_buffer, raw_int_size);
            payload_size = raw_int_size;
        } else {
            payload_size = lz_size;
        }
    }

    // 3. Montagem: Coloca o Payload logo após o Cabeçalho
    memcpy(_c_buffer + HEADER_SIZE, _work_buffer, payload_size);
    int total_packet_size = HEADER_SIZE + payload_size;

    // 4. Aplica criptografia se habilitada
    _apply_encryption(_c_buffer, total_packet_size, nid);

    CosmicPacket pkg;
    pkg.data = _c_buffer;
    pkg.size = total_packet_size;
    pkg.type = type;
    pkg.mode = mod;
    return pkg;
}

/**
 * @brief uppkg (Unpack Floats) - Desempacota dados de telemetria
 * @param packet Pacote recebido (já descriptografado se necessário)
 * @param packet_size Tamanho do pacote
 * @param output Buffer para floats desempacotados
 * @param max_output Número máximo de floats no buffer
 * @return Número de floats desempacotados, ou -1 em caso de erro
 */
int uppkg(const uint8_t* packet, uint8_t packet_size, float* output, int max_output) {
    if (packet_size < HEADER_SIZE) return -1;
    
    uint8_t type = packet[2];
    uint8_t mode = packet[3];
    
    // Verifica se é pacote de telemetria
    if (type != PKG_TYPE_TELEMETRY) return -1;
    
    int payload_size = packet_size - HEADER_SIZE;
    const uint8_t* payload = packet + HEADER_SIZE;
    
    if (mode == COMPRESS_NONE) {
        // Modo RAW: copia floats diretamente
        int num_floats = payload_size / sizeof(float);
        if (num_floats > max_output) num_floats = max_output;
        
        memcpy(output, payload, num_floats * sizeof(float));
        return num_floats;
    }
    else if (mode == COMPRESS_COSMIC) {
        // Modo COSMIC: descomprime e processa delta encoding
        int decompressed_size = fastlz_decompress(payload, payload_size, _work_buffer, sizeof(_work_buffer));
        if (decompressed_size <= 0) return -1;
        
        int num_ints = decompressed_size / sizeof(int16_t);
        if (num_ints > max_output) num_ints = max_output;
        
        int16_t* int_data = (int16_t*)_work_buffer;
        
        // Processa delta encoding inverso
        float cumulative = int_data[0] / 100.0;
        output[0] = cumulative;
        
        for (int i = 1; i < num_ints; i++) {
            cumulative += int_data[i] / 100.0;
            output[i] = cumulative;
        }
        
        return num_ints;
    }
    
    return -1; // Modo não reconhecido
}

// =================================================================================
// API PÚBLICA - IMAGENS (NOVAS FUNÇÕES)
// =================================================================================

/**
 * @brief ppkg_image (Pack Image) - Comprime e empacota imagem
 * @param nid Network ID
 * @param did Device ID
 * @param type Tipo de pacote (usar PKG_TYPE_IMAGE)
 * @param compress_mode Modo de compressão (IMG_COMPRESS_*)
 * @param pixels Array de pixels em escala de cinza (8-bit)
 * @param width Largura da imagem (1-255)
 * @param height Altura da imagem (1-255)
 * @return Pacote de imagem pronto para transmissão
 */
CosmicImagePacket ppkg_image(uint8_t nid, uint8_t did, uint8_t type, uint8_t compress_mode,
                             const uint8_t* pixels, uint8_t width, uint8_t height) {
    CosmicImagePacket img_pkt = {0};
    
    // Verifica tamanho da imagem
    uint16_t img_size = width * height;
    if (img_size == 0 || img_size > MAX_IMAGE_SIZE) {
        // Reduz automaticamente se muito grande
        width = 16;
        height = 16;
        img_size = 256;
    }
    
    // 1. Comprime a imagem usando a biblioteca img_compress
    ImgCompressMode img_mode;
    switch(compress_mode) {
        case COMPRESS_IMG_RLE:   img_mode = IMG_COMPRESS_RLE; break;
        case COMPRESS_IMG_BLOCK: img_mode = IMG_COMPRESS_BLOCK4; break;
        case COMPRESS_IMG_DOWN2: img_mode = IMG_COMPRESS_DOWN2; break;
        default:                 img_mode = IMG_COMPRESS_NONE; break;
    }
    
    CompressedImage cimg = img_compress(pixels, width, height, img_mode);
    
    // 2. Prepara cabeçalho (igual aos outros pacotes)
    _prepare_header(nid, did, type, compress_mode);
    
    // 3. Copia dados comprimidos após o header
    int total_size = HEADER_SIZE + cimg.size;
    if (total_size > MAX_COSMIC_BUFFER) {
        total_size = MAX_COSMIC_BUFFER;
    }
    
    memcpy(_c_buffer + HEADER_SIZE, cimg.data, cimg.size);
    
    // 4. Aplica criptografia se habilitada
    _apply_encryption(_c_buffer, total_size, nid);
    
    // 5. Preenche estrutura de retorno
    img_pkt.data = _c_buffer;
    img_pkt.size = total_size;
    img_pkt.img_width = width;
    img_pkt.img_height = height;
    img_pkt.compress_mode = compress_mode;
    
    return img_pkt;
}

/**
 * @brief uppkg_image (Unpack Image) - Desempacota e descomprime imagem
 * @param packet Pacote recebido (já descriptografado se necessário)
 * @param packet_size Tamanho do pacote
 * @param output Buffer para imagem descomprimida
 * @param max_output Tamanho máximo do buffer em bytes
 * @return 1 se sucesso, 0 se erro
 */
int uppkg_image(const uint8_t* packet, uint8_t packet_size, 
                uint8_t* output, uint16_t max_output) {
    if (packet_size < HEADER_SIZE + 3) {
        return 0;  // Pacote muito pequeno
    }
    
    // Verifica se é pacote de imagem
    uint8_t type = packet[2];
    if (type != PKG_TYPE_IMAGE) {
        return 0;  // Não é pacote de imagem
    }
    
    // Extrai informações da imagem do payload
    uint8_t width = packet[HEADER_SIZE];
    uint8_t height = packet[HEADER_SIZE + 1];
    uint8_t mode = packet[HEADER_SIZE + 2];
    
    uint16_t expected_size = width * height;
    if (expected_size == 0 || expected_size > max_output) {
        return 0;  // Tamanho inválido ou buffer pequeno
    }
    
    // Cria estrutura CompressedImage para a biblioteca img_compress
    CompressedImage cimg;
    cimg.data = (uint8_t*)(packet + HEADER_SIZE);
    cimg.size = packet_size - HEADER_SIZE;
    cimg.mode = mode;
    cimg.original_width = width;
    cimg.original_height = height;
    
    // Descomprime usando a biblioteca img_compress
    return img_decompress(&cimg, output);
}

/**
 * @brief create_test_image - Cria imagem de teste (cruz)
 * @param width Largura da imagem
 * @param height Altura da imagem
 * @param buffer Buffer para imagem (deve ter espaço para width*height bytes)
 */
void create_test_image(uint8_t width, uint8_t height, uint8_t* buffer) {
    for (uint8_t y = 0; y < height; y++) {
        for (uint8_t x = 0; x < width; x++) {
            // Cria um padrão de cruz simples
            if (x == width/2 || y == height/2 || 
                x == width/2 - 1 || y == height/2 - 1) {
                buffer[y * width + x] = 255;  // Branco
            } else {
                buffer[y * width + x] = 0;    // Preto
            }
        }
    }
}

// =================================================================================
// API PÚBLICA - UTILITÁRIOS
// =================================================================================

/**
 * @brief decrypt_packet - Descriptografa pacote recebido
 * @param packet Pacote a ser descriptografado
 * @param size Tamanho do pacote
 * @param net_id Network ID esperado (para IV)
 * @param counter Contador de pacotes esperado
 * @return 1 se descriptografado, 0 se não
 */
int decrypt_packet(uint8_t* packet, uint8_t size, uint8_t net_id, uint32_t counter) {
    if (!_encryption_enabled) return 0;
    
    uint8_t iv[16];
    _prepare_iv(iv, net_id, counter);
    
    // Aplica operação XOR novamente para descriptografar (CTR é simétrico)
    maes_ctr_process(packet, size, iv, _cosmic_key);
    return 1;
}

/**
 * @brief get_packet_info - Extrai informações do cabeçalho do pacote
 * @param packet Pacote (criptografado ou não)
 * @param net_id Ponteiro para Network ID (saída)
 * @param dev_id Ponteiro para Device ID (saída)
 * @param type Ponteiro para tipo de pacote (saída)
 * @param mode Ponteiro para modo (saída)
 * @return 1 se sucesso, 0 se erro
 */
int get_packet_info(const uint8_t* packet, uint8_t* net_id, uint8_t* dev_id, 
                    uint8_t* type, uint8_t* mode) {
    if (!packet) return 0;
    
    // Se criptografia está habilitada, não podemos ler o cabeçalho diretamente
    // O chamador deve descriptografar primeiro
    if (_encryption_enabled) {
        // Retorna 0 para indicar que precisa descriptografar primeiro
        return 0;
    }
    
    *net_id = packet[0];
    *dev_id = packet[1];
    *type = packet[2];
    *mode = packet[3];
    
    return 1;
}

/**
 * @brief calculate_crc8 - Calcula CRC-8 simples para verificação de integridade
 * @param data Dados a serem verificados
 * @param length Tamanho dos dados
 * @return CRC-8 calculado
 */
uint8_t calculate_crc8(const uint8_t* data, uint8_t length) {
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;  // Polinômio x^8 + x^2 + x + 1
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

#endif // COSMIC_PAYLOAD_H