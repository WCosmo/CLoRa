#ifndef COSMIC_PAYLOAD_H
#define COSMIC_PAYLOAD_H

#include <Arduino.h>
#include "fastlz.h" 
#include "mini_aes.h" 

// --- Configurações ---
#define MAX_COSMIC_BUFFER 256
#define HEADER_SIZE 4

// --- Buffers Internos ---
static uint8_t _c_buffer[MAX_COSMIC_BUFFER];      // Buffer Final (Header + Payload)
static uint8_t _work_buffer[MAX_COSMIC_BUFFER];   // Buffer Temporário de Trabalho

// Buffer para conversão int16
static int16_t _raw_int_buffer[MAX_COSMIC_BUFFER / 2]; 

// --- Estado da Criptografia ---
static uint8_t _cosmic_key[16] = {0};
static bool _encryption_enabled = false;

struct CosmicPacket {
    uint8_t* data;
    uint8_t size;
};

// =================================================================================
// FUNÇÕES INTERNAS
// =================================================================================

void _prepare_header(uint8_t net_id, uint8_t dev_id, uint8_t pkg_type, uint8_t mod) {
    _c_buffer[0] = net_id;
    _c_buffer[1] = dev_id;
    _c_buffer[2] = pkg_type;
    _c_buffer[3] = mod;
}

// =================================================================================
// API PÚBLICA UNIVERSAL (PPKG)
// =================================================================================

void setCosmicKey(const uint8_t key[16]) {
    memcpy(_cosmic_key, key, 16);
    _encryption_enabled = true;
}

void disableEncryption() {
    _encryption_enabled = false;
}

/**
 * @brief ppkg (Pack of Floats)
 * Encripta o pacote INTEIRO usando IV derivado apenas do NET_ID.
 */
CosmicPacket ppkg(bool compress, uint8_t nid, uint8_t did, uint8_t type, uint8_t mod, float* pack, int n) {
    // 1. Escreve Cabeçalho no buffer final (Texto Claro por enquanto)
    _prepare_header(nid, did, type, mod);

    int max_floats = (MAX_COSMIC_BUFFER - HEADER_SIZE) / (compress ? 2 : 4);
    if (n > max_floats) n = max_floats;
    
    // Tratamento para pacote vazio (apenas Header)
    if (n <= 0) {
        if (_encryption_enabled) {
            uint8_t iv[16] = {0};
            iv[0] = nid; // IV = [NET_ID, 0, 0, ...]
            maes_ctr_process(_c_buffer, HEADER_SIZE, iv, _cosmic_key);
        }
        CosmicPacket pkg; pkg.data = _c_buffer; pkg.size = HEADER_SIZE; return pkg;
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

    // 4. CRIPTOGRAFIA TOTAL (Header + Payload)
    if (_encryption_enabled) {
        // Constrói o IV usando APENAS o NET_ID
        uint8_t iv[16] = {0}; // Zera tudo
        iv[0] = nid;          // Define o primeiro byte como NET_ID
        
        // Aplica AES-CTR no pacote inteiro
        // Se o receptor não tiver a mesma chave E o mesmo NET_ID configurado no seu lado,
        // a descriptografia gerará lixo.
        maes_ctr_process(_c_buffer, total_packet_size, iv, _cosmic_key);
    }

    CosmicPacket pkg;
    pkg.data = _c_buffer;
    pkg.size = total_packet_size;
    return pkg;
}

#endif