int main(){int r=0;for(int i=0;i<50000000;i++)r=r+__builtin_popcount(i)+__builtin_popcount(~i);return r&255;}
