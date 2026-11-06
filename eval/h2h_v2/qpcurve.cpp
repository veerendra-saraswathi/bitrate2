#include <cstdio>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <vector>
std::vector<uint8_t> encode_frame(const std::vector<uint8_t>&, int, int, int, int, int);
std::vector<uint8_t> decode_frame(const std::vector<uint8_t>&);
int main(int, char** argv){ std::vector<uint8_t> px(640*512); std::ifstream(argv[1],std::ios::binary).read((char*)px.data(),px.size());
 for(int L: {1,3,6}) for(int q: {8,16,24,32,40,42,43,48,64,96,128,192,255}){ auto bs=encode_frame(px,640,512,q,L,1); auto d=decode_frame(bs); double m=0; for(size_t i=0;i<px.size();++i){double e=double(d[i])-px[i]; m+=e*e;} m/=px.size();
 printf("L%d q%3d bytes %7zu psnr %.2f\n",L,q,bs.size(),10*log10(65025/m)); } }
