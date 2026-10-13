#include "velocita/v2/swap_submission_store.hpp"
#include "velocita/v2/digest.hpp"
#include <iostream>
using namespace velocita::v2;
int main(int argc,char**argv){if(argc!=5)return 2;
 auto inc=parse_digest256(argv[2]),old=parse_digest256(argv[3]),next=parse_digest256(argv[4]);if(!inc||!old||!next)return 2;
 {SwapSubmissionStore s(argv[1],{*inc,*old});if(s.recover()!=SwapSubmissionCode::corrupt)return 3;}
 {SwapSubmissionStore s(argv[1],{*inc,*next});if(s.recover()!=SwapSubmissionCode::none)return 4;
 const auto&r=s.latest();if(r.ordinal!=2||r.phase!=SwapSubmissionPhase::idle||r.identity.effect.value()!=0||r.committed_index!=6)return 5;}
 std::cout<<"PASS native recovery accepts renewed idle ordinal2 and rejects old policy domain\n";
}
