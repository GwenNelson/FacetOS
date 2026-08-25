#include <facetos/sha256.h>
#include <string.h>

typedef struct { uint64_t length; uint32_t h[8]; uint8_t buffer[64]; } State;
static uint32_t rr(uint32_t x,unsigned n){return(x>>n)|(x<<(32u-n));}
static const uint32_t constants[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

static void block(State*s,const uint8_t*p){uint32_t w[64];for(unsigned i=0;i<16;i++)w[i]=(uint32_t)p[i*4]<<24|(uint32_t)p[i*4+1]<<16|(uint32_t)p[i*4+2]<<8|p[i*4+3];for(unsigned i=16;i<64;i++)w[i]=w[i-16]+(rr(w[i-15],7)^rr(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(rr(w[i-2],17)^rr(w[i-2],19)^(w[i-2]>>10));uint32_t a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];for(unsigned i=0;i<64;i++){uint32_t x=h+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^(~e&g))+constants[i]+w[i];uint32_t y=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c));h=g;g=f;f=e;e=d+x;d=c;c=b;b=a;a=x+y;}s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;}
static void init(State*s){memset(s,0,sizeof(*s));uint32_t v[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};for(unsigned i=0;i<8;i++)s->h[i]=v[i];}
static void update(State*s,const uint8_t*p,size_t n){size_t used=(size_t)(s->length%64);s->length+=n;if(used){size_t take=64-used;if(take>n)take=n;memcpy(s->buffer+used,p,take);used+=take;p+=take;n-=take;if(used==64)block(s,s->buffer);}while(n>=64){block(s,p);p+=64;n-=64;}if(n)memcpy(s->buffer,p,n);}
static void finish(State*s,uint8_t*out){size_t used=(size_t)(s->length%64);s->buffer[used++]=0x80;if(used>56){memset(s->buffer+used,0,64-used);block(s,s->buffer);used=0;}memset(s->buffer+used,0,56-used);uint64_t bits=s->length*8;for(unsigned i=0;i<8;i++)s->buffer[63-i]=(uint8_t)(bits>>(i*8));block(s,s->buffer);for(unsigned i=0;i<8;i++){out[i*4]=(uint8_t)(s->h[i]>>24);out[i*4+1]=(uint8_t)(s->h[i]>>16);out[i*4+2]=(uint8_t)(s->h[i]>>8);out[i*4+3]=(uint8_t)s->h[i];}}
void facet_sha256(const uint8_t*data,size_t size,uint8_t digest[32]){State s;init(&s);update(&s,data,size);finish(&s,digest);}
void facet_sha256_hex(const uint8_t d[32],char o[65]){static const char x[]="0123456789abcdef";for(unsigned i=0;i<32;i++){o[i*2]=x[d[i]>>4];o[i*2+1]=x[d[i]&15];}o[64]=0;}
