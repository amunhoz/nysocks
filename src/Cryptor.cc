// The encryption part is imported from:
// https://github.com/saju/misc/blob/master/misc/openssl_aes.c
#include "Cryptor.h"
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

static void encode_random_bytes(unsigned char *content, int position) {
  RAND_bytes(content + position, KCPUV_NONCE_LENGTH);
}

/**
 * Create a 256 bit key and IV using the supplied key_data. salt can be added
 *for taste. Fills in the encryption and decryption ctx objects and returns 0
 *on success
 **/
static int aes_init(unsigned char *key_data, int key_data_len,
                    unsigned char *salt, EVP_CIPHER_CTX *e_ctx,
                    EVP_CIPHER_CTX *d_ctx) {
  int i, nrounds = 5;
  unsigned char key[32], iv[32];

  /*
   * Gen key & IV for AES 256 CBC mode. A SHA1 digest is used to hash the
   * supplied key material. nrounds is the number of times the we hash the
   * material. More rounds are more secure but slower.
   */
  i = EVP_BytesToKey(EVP_aes_256_cbc(), EVP_sha1(), salt, key_data,
                     key_data_len, nrounds, key, iv);
  if (i != 32) {
    printf("Key size is %d bits - should be 256 bits\n", i);
    return -1;
  }

  EVP_CIPHER_CTX_init(e_ctx);
  EVP_EncryptInit_ex(e_ctx, EVP_aes_256_cbc(), NULL, key, iv);
  EVP_CIPHER_CTX_init(d_ctx);
  EVP_DecryptInit_ex(d_ctx, EVP_aes_256_cbc(), NULL, key, iv);

  return 0;
}

namespace kcpuv {
/*
 * Encrypt *len bytes of data
 * All data going in & out is considered binary (unsigned char[])
 */
unsigned char *Cryptor::KcpuvCryptorEncrypt(kcpuv_cryptor *cryptor,
                                            unsigned char *plaintext,
                                            int *len) {
  /* max ciphertext len for a n bytes of plaintext is n + AES_BLOCK_SIZE -1
   * bytes */
  int c_len = *len + AES_BLOCK_SIZE, f_len = 0;
  unsigned char *ciphertext = new unsigned char[c_len];
  EVP_CIPHER_CTX *e = reinterpret_cast<EVP_CIPHER_CTX *>(cryptor->en);

  /* allows reusing of 'e' for multiple encryption cycles */
  EVP_EncryptInit_ex(e, NULL, NULL, NULL, NULL);

  /* update ciphertext, c_len is filled with the length of ciphertext
   *generated, len is the size of plaintext in bytes */
  EVP_EncryptUpdate(e, ciphertext, &c_len, plaintext, *len);

  /* update ciphertext with the final remaining bytes */
  EVP_EncryptFinal_ex(e, ciphertext + c_len, &f_len);

  *len = c_len + f_len;
  return ciphertext;
}

/*
 * Decrypt *len bytes of ciphertext
 */
unsigned char *Cryptor::KcpuvCryptorDecrypt(kcpuv_cryptor *cryptor,
                                            unsigned char *ciphertext,
                                            int *len) {
  EVP_CIPHER_CTX *e = cryptor->de;
  /* plaintext will always be equal to or lesser than length of ciphertext*/
  int p_len = *len, f_len = 0;
  unsigned char *plaintext = new unsigned char[p_len];

  EVP_DecryptInit_ex(e, NULL, NULL, NULL, NULL);
  EVP_DecryptUpdate(e, plaintext, &p_len, ciphertext, *len);
  EVP_DecryptFinal_ex(e, plaintext + p_len, &f_len);

  *len = p_len + f_len;
  return plaintext;
}

// TODO: salt should be chars
int Cryptor::KcpuvCryptorInit(kcpuv_cryptor *cryptor, const char *key,
                              int key_len, unsigned int salt[]) {

// NOTE: Openssl has changed this api from 1.1.0
#if OPENSSL_VERSION_NUMBER >= 0x010100000
  cryptor->en = EVP_CIPHER_CTX_new();
  cryptor->de = EVP_CIPHER_CTX_new();
#endif
#if OPENSSL_VERSION_NUMBER < 0x010100000
  cryptor->en =
      reinterpret_cast<EVP_CIPHER_CTX *>(new char[sizeof(EVP_CIPHER_CTX)]);
  cryptor->de =
      reinterpret_cast<EVP_CIPHER_CTX *>(new char[sizeof(EVP_CIPHER_CTX)]);
#endif

  if (aes_init((unsigned char *)key, key_len, (unsigned char *)salt,
               cryptor->en, cryptor->de)) {
    fprintf(stderr, "Couldn't initialize AES cipher\n");
    return -1;
  }

  return 0;
}

void Cryptor::KcpuvCryptorClean(kcpuv_cryptor *cryptor) {
  EVP_CIPHER_CTX_cleanup(cryptor->en);
  EVP_CIPHER_CTX_cleanup(cryptor->de);
  // TODO: Check different node/openssl version.
  free(cryptor->en);
  free(cryptor->de);
}

int Cryptor::KcpuvProtocolDecode(const char *content) {
  // int ver = (*(const unsigned char *)content) & 0xff;
  int cmd = (*(const unsigned char *)(content + 1)) & 0xff;
  return cmd;
}

void Cryptor::KcpuvProtocolEncode(int cmd, char *content) {
  // NOTE: reserved
  content[0] = KCPUV_PROTOCOL_VERSION;
  content[1] = cmd;
  encode_random_bytes((unsigned char *)content, 2);
}
} // namespace kcpuv
