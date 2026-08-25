#include <facetos/sha256.h>

#include <string.h>

static const uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
    0xca273ece,0xd186b8c7,0xeada7dd6,0xf57d4f7,0x06f067aa,0x0a637dc5,0x113f9804,0x1b710b35,
    0x28db77f5,0x32caab7b,0x3c9ebe0a,0x431d67c4,0x4cc5d4be,0x597f299c,0x5fcb6fab,0x6c44198c
};
static uint32_t r(uint32_t x, unsigned n) { return (x >> n) | (x << (32-n)); }
static void block(uint32_t h[8], const uint8_t p[64]) {
    uint32_t w[64];
    for (unsigned i=0;i<16;i++) w[i]=((uint32_t)p[4*i]<<24)|((uint32_t)p[4*i+1]<<16)|((uint32_t)p[4*i+2]<<8)|p[4*i+3];
    for (unsigned i=16;i<64;i++) w[i]=(r(w[i-2],17)^r(w[i-2],19)^(w[i-2]>>10))+w[i-7]+(r(w[i-15],7)^r(w[i-15],18)^(w[i-15]>>3))+w[i-16];
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],x=h[7];
    for (unsigned i=0;i<64;i++) { uint32_t t1=x+(r(e,6)^r(e,11)^r(e,25))+((e&f)^((~e)&g))+k[i]+w[i]; uint32_t t2=(r(a,2)^r(a,13)^r(a,22))+((a&b)^(a&c)^(b&c)); x=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=x;
}
void facet_sha256(const uint8_t *data, size_t size, uint8_t out[32]) {
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    size_t n=size; while(n>=64){block(h,data);data+=64;n-=64;} uint8_t p[128]={0}; if(n)memcpy(p,data,n);p[n]=0x80; uint64_t bits=(uint64_t)size*8; size_t end=n<56?56:120; for(unsigned i=0;i<8;i++)p[end+7-i]=(uint8_t)(bits>>(8*i)); block(h,p); if(end==120)block(h,p+64);
    for(unsigned i=0;i<8;i++)for(unsigned j=0;j<4;j++)out[4*i+j]=(uint8_t)(h[i]>>(24-8*j));
}
void facet_sha256_hex(const uint8_t digest[32], char output[65]) { static const char hex[]="0123456789abcdef"; for(unsigned i=0;i<32;i++){output[i*2]=hex[digest[i]>>4];output[i*2+1]=hex[digest[i]&15];} output[64]=0; }
