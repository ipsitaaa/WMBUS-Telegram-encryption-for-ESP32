#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cinttypes>
#include <cctype>
#include <vector>
#include <string>
#include "mbedtls/aes.h"

class HexUtils {
public:
    static int hexToBytes(const char* hexStr, uint8_t* bytes, size_t maxLen) {
        size_t len = strlen(hexStr);
        if (len % 2 != 0) return -1;
        size_t byteLen = len / 2;
        if (byteLen > maxLen) return -1;
        
        for (size_t i = 0; i < byteLen; i++) {
            char high = tolower(hexStr[i * 2]);
            char low = tolower(hexStr[i * 2 + 1]);
            uint8_t h = (high >= 'a') ? (high - 'a' + 10) : (high - '0');
            uint8_t l = (low >= 'a') ? (low - 'a' + 10) : (low - '0');
            bytes[i] = (h << 4) | l;
        }
        return byteLen;
    }
};

class AESCrypto {
private:
    mbedtls_aes_context aesContext;
public:
    AESCrypto() { mbedtls_aes_init(&aesContext); }
    ~AESCrypto() { mbedtls_aes_free(&aesContext); }
    
    int decryptCBC(const uint8_t* key, const uint8_t* iv, 
                   const uint8_t* input, size_t inputLen, uint8_t* output) {
        if (inputLen % 16 != 0) return -1;
        if (mbedtls_aes_setkey_dec(&aesContext, key, 128) != 0) return -1;
        uint8_t ivCopy[16];
        memcpy(ivCopy, iv, 16);
        return mbedtls_aes_crypt_cbc(&aesContext, MBEDTLS_AES_DECRYPT, 
                                     inputLen, ivCopy, input, output);
    }
};


class WMBusTelegram {
private:
    std::vector<uint8_t> data;
public:
    WMBusTelegram(const uint8_t* telegramData, size_t length) 
        : data(telegramData, telegramData + length) {}
    
    size_t getLength() const { return data.size(); }
    const uint8_t* getData() const { return data.data(); }
    
    
    void printStructure() const {
        printf("\n=== Telegram Structure ===\n");
        printf("L-field:    0x%02X (%zu bytes)\n", data[0], data.size());
        printf("C-field:    0x%02X\n", data[1]);
        printf("M-field:    0x%02X%02X\n", data[3], data[2]);
        printf("Serial:     %02X%02X%02X%02X\n", data[7], data[6], data[5], data[4]);
        printf("Version:    0x%02X\n", data[8]);
        printf("Type:       0x%02X (Water meter)\n", data[9]);
        printf("ELL-ACC:    0x%02X\n", data[12]);
        printf("TPL-ACC:    0x%02X\n", data[14]);
        uint16_t cfg = (data[17] << 8) | data[16];
        int mode = (cfg >> 8) & 0x0F;
        printf("TPL-CFG:    0x%04X -> Mode %d (AES-128 CBC-IV)\n", cfg, mode);
    }
    
    uint32_t getSerial() const {
        return data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    }
};

class OMSDataParser {
private:
    const uint8_t* data;
    size_t len;
    size_t pos;
    int recordNum;
    
    double totalVolume;
    uint32_t meterDateTime;
    uint8_t status;
    bool hasVolume, hasDateTime, hasStatus;
    
    
    void decodeDateTime(uint32_t raw) {
        int minute = (raw >> 0) & 0x3F;
        int hour = (raw >> 8) & 0x1F;
        int day = (raw >> 16) & 0x1F;
        int month = (raw >> 21) & 0x0F;
        
        int year = 2000 + (((raw >> 25) & 0x7F) - 3); 
        
        printf("%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
    }
    
    void decodeDate(uint16_t raw) {
        if (raw == 0xFFFF) {
            printf("Not set");
            return;
        }
        int day = raw & 0x1F;
        int month = (raw >> 5) & 0x0F;
        int year = 2000 + ((raw >> 9) & 0x7F);
        printf("%04d-%02d-%02d", year, month, day);
    }
    
public:
    OMSDataParser(const uint8_t* d, size_t l) 
        : data(d), len(l), pos(2), recordNum(1),
          totalVolume(-1.0), meterDateTime(0), status(0),
          hasVolume(false), hasDateTime(false), hasStatus(false) {}
     
    void parseAll() {
        printf("\n=== Decoded Meter Data ===\n\n");
        
        while (pos < len && data[pos] != 0x2F) {
            if (len - pos < 1) break;
            
            printf("Record %d: ", recordNum++);
            
            uint8_t dif = data[pos++];
            int dataField = dif & 0x0F;
            int storage = 0;
            if (dif & 0x40) storage = 1;

            if ((dif & 0x80) && pos < len) {
                uint8_t dife = data[pos++];
                storage = (dife & 0x0F);
                
            }
            
            if (pos >= len) break;
            
            uint8_t vifOriginal = data[pos++];
            uint8_t vif = vifOriginal;
            
            std::vector<uint8_t> vifExtensions;
            while ((vif & 0x80) && pos < len) {
                vif = data[pos++];
                vifExtensions.push_back(vif);
            }

            int dataLen = 0;
            switch (dataField) {
                case 0x00: dataLen = 0; break;
                case 0x01: dataLen = 1; break;
                case 0x02: dataLen = 2; break;
                case 0x03: dataLen = 3; break;
                case 0x04: dataLen = 4; break;
                case 0x05: dataLen = 4; break; 
                case 0x06: dataLen = 6; break;
                case 0x07: dataLen = 8; break;
                case 0x09: dataLen = 1; break; 
                case 0x0A: dataLen = 2; break; 
                case 0x0B: dataLen = 3; break; 
                case 0x0C: dataLen = 4; break; 
                case 0x0D:
                    if (pos < len) dataLen = data[pos++];
                    break;
                default: dataLen = 0;
            }
            
            if (pos + dataLen > len) {
                printf("Incomplete record\n");
                break;
            }
            
            uint32_t val32 = 0;
            if (dataLen > 0) {
                val32 = data[pos];
                if (dataLen > 1) val32 |= (uint32_t)data[pos+1] << 8;
                if (dataLen > 2) val32 |= (uint32_t)data[pos+2] << 16;
                if (dataLen > 3) val32 |= (uint32_t)data[pos+3] << 24;
            }

            if (vifOriginal == 0x6D && dataLen == 4) { 
                printf("Meter Date/Time = ");
                decodeDateTime(val32);
                meterDateTime = val32;
                hasDateTime = true;
            } else if ((vifOriginal & 0x7F) == 0x13 && dataLen == 4) { 
                double vol_m3 = (int32_t)val32 / 1000.0;
                bool isBackflow = false;
                if (!vifExtensions.empty() && (vifExtensions[0] & 0x7F) == 0x3C) {
                    isBackflow = true;
                }

                if(isBackflow) {
                    printf("Backflow = %.3f m3", vol_m3);
                } else {
                    printf("Volume = %.3f m3", vol_m3);
                    if (val32 == 0xFFFFFFFF) printf(" (Not available)");
                }
                
                if (storage > 0) {
                    printf(" (History %d)", storage);
                } else if (!hasVolume && !isBackflow) {
                    totalVolume = vol_m3;
                    hasVolume = true;
                }
            } else if (vifOriginal == 0xFD && !vifExtensions.empty() && vifExtensions[0] == 0x17 && dataLen == 1) { 
                uint8_t stat = data[pos];
                printf("Status = 0x%02X (%s)", stat, stat == 0 ? "OK" : "Error");
                status = stat;
                hasStatus = true;
            } else if (vifOriginal == 0x6C && dataLen == 2) { 
                printf("Date = ");
                decodeDate(val32 & 0xFFFF);
                 if (storage > 0) printf(" (History %d)", storage);
            } else {
                printf("Unknown record: DIF=0x%02X, VIF=0x%02X, Data=", dif, vifOriginal);
                for(int i=0; i<dataLen; ++i) printf("%02X ", data[pos+i]);
            }
            
            printf("\n");
            pos += dataLen;
        }
    }
    
    double getTotalVolume() const { return totalVolume; }
    uint32_t getDateTime() const { return meterDateTime; }
    uint8_t getStatus() const { return status; }
    bool hasValidVolume() const { return hasVolume; }
    bool hasValidDateTime() const { return hasDateTime; }
    bool hasValidStatus() const { return hasStatus; }
};

class OMSDecryptor {
private:
    AESCrypto crypto;
    
    void constructIV(const WMBusTelegram& telegram, uint8_t* iv) {
        const uint8_t* data = telegram.getData();
        iv[0] = data[2]; 
        iv[1] = data[3]; 
        memcpy(&iv[2], &data[4], 6); 
        iv[8] = data[12]; 
        iv[9] = data[14]; 
        memset(&iv[10], 0x00, 6);}
    
public:
    int decrypt(const WMBusTelegram& telegram, const uint8_t* key,
                std::vector<uint8_t>& decrypted) {
        
        if (telegram.getLength() < 18) return -1;
        
        telegram.printStructure();
        
        uint8_t iv[16];
        constructIV(telegram, iv);
        printf("\nIV: ");
        for (int i = 0; i < 16; i++) printf("%02X ", iv[i]);
        printf("\n");
        
        const uint8_t* encryptedPayload = telegram.getData() + 18;
        size_t payloadLen = telegram.getLength() - 18;
        
        if (payloadLen % 16 != 0) {
            payloadLen = (payloadLen / 16) * 16;
        }
        
        decrypted.resize(payloadLen);
        int result = crypto.decryptCBC(key, iv, encryptedPayload, 
                                      payloadLen, decrypted.data());
        
        if (result == 0) {
            if (decrypted[0] == 0x2F && decrypted[1] == 0x2F) {
                printf("\nDecryption: SUCCESS (0x2F2F verified)\n");
                return 0;
            } else {
                printf("\nDecryption: FAILED (Verification bytes 0x2F2F not found)\n");
                return -1;
            }
        }
        return result;
    }
    
    void parseData(const std::vector<uint8_t>& data, uint32_t serialNumber) {
        OMSDataParser parser(data.data(), data.size());
        parser.parseAll();
        
        printf("\n=== Summary ===\n");
        printf("Meter ID:          %08" PRIX32 "\n", serialNumber);
        
        if (parser.hasValidVolume()) {
            printf("Total Consumption: %.3f m3\n", parser.getTotalVolume());
        }
        
        if (parser.hasValidStatus()) {
            printf("Meter Status:      %s\n", 
                   parser.getStatus() == 0 ? "OK" : "ERROR");
        }
        
        if (parser.hasValidDateTime()) {
            uint32_t dt = parser.getDateTime();
            int minute = (dt >> 0) & 0x3F;
            int hour = (dt >> 8) & 0x1F;
            int day = (dt >> 16) & 0x1F;
            int month = (dt >> 21) & 0x0F;
            int year = 2000 + (((dt >> 25) & 0x7F) - 3); 
            printf("Timestamp:         %04d-%02d-%02d %02d:%02d\n", 
                   year, month, day, hour, minute);
        }
        
        printf("\n=== END OF DATA ===\n");
    }
};

class WMBusApplication {
private:
    OMSDecryptor decryptor;
    
public:
    void run() {
        printf("\n================================================\n");
        printf("  W-MBus OMS Telegram Decryption\n");
        printf("  AES-128 CBC-IV (OMS Volume 2 Mode 5)\n");
        printf("================================================\n");
        
        const char* keyHex = "4255794d3dccfd46953146e701b7db68";
        uint8_t key[16];
        
        if (HexUtils::hexToBytes(keyHex, key, sizeof(key)) != 16) {
            printf("Error: Invalid key\n");
            return;
        }
        
        printf("\nAES-128 Key: ");
        for (int i = 0; i < 16; i++) printf("%02X ", key[i]);
        printf("\n");
        
        const char* telegramHex = 
            "a144c5142785895070078c20607a9d00902537ca231fa2da5889be8df367"
            "3ec136aebfb80d4ce395ba98f6b3844a115e4be1b1c9f0a2d5ffbb92906aa388deaa"
            "82c929310e9e5c4c0922a784df89cf0ded833be8da996eb5885409b6c9867978dea"
            "24001d68c603408d758a1e2b91c42ebad86a9b9d287880083bb0702850574d7b51"
            "e9c209ed68e0374e9b01febfd92b4cb9410fdeaf7fb526b742dc9a8d0682653";
        
        uint8_t telegramBuffer[512];
        int telegramLen = HexUtils::hexToBytes(telegramHex, telegramBuffer, 
                                               sizeof(telegramBuffer));
        
        if (telegramLen < 0) {
            printf("Error: Invalid telegram\n");
            return;
        }
        
        WMBusTelegram telegram(telegramBuffer, telegramLen);
        std::vector<uint8_t> decrypted;
        
        if (decryptor.decrypt(telegram, key, decrypted) == 0) {
            decryptor.parseData(decrypted, telegram.getSerial());
            printf("\n================================================\n");
        } else {
            printf("\nDecryption failed\n");
        }
    }
};

extern "C" void app_main(void) {
    WMBusApplication app;
    app.run();
}

int main() {
    app_main();
    return 0;
}

