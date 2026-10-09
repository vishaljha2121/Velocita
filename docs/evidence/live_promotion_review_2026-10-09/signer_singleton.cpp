#include "velocita/v2/signer_service.hpp"
#include <iostream>
#include <unistd.h>
using namespace velocita::v2::signer;
int main(int argc,char**argv) {
 if(argc!=2)return 2;
 std::filesystem::path root=argv[1]; std::filesystem::create_directories(root);
 auto file=root/"floor";
 if(FloorFile::create(file)!=FloorError::none)return 3;
 FloorFile a,b,reloaded;
 if(FloorFile::open(file,a)!=FloorError::none || FloorFile::open(file,b)!=FloorError::none)return 4;
 auto as=a.store(),bs=b.store();
 bool first=as.persist_fn(as.context,100),second=bs.persist_fn(bs.context,1);
 if(FloorFile::open(file,reloaded)!=FloorError::none)return 5;
 std::cout<<"first_write_100="<<first<<" stale_second_write_1="<<second<<" durable_floor="<<reloaded.floor()<<"\n";
 SignerServer one,two; auto sock=root/"s";
 bool s1=one.listen(sock,geteuid()),s2=two.listen(sock,geteuid());
 std::cout<<"first_listener="<<s1<<" second_listener_same_path="<<s2<<"\n";
 return (first && second && reloaded.floor()==1 && s1 && s2)?0:1;
}
