#include <string.h>
static int cjson_int(const char*buf,const char*key){const char*k=strstr(buf,key);if(!k)return -1;const char*i=k+strlen(key);while(*i!=':')i++;i++;while(*i==' ')i++;int neg=0;if(*i=='-'){neg=1;i++;}int n=0;while(*i>='0'&&*i<='9'){n=n*10+(*i-48);i++;}return neg?-n:n;}
int main(){const char*j="{\"a\":1,\"port\":8080,\"timeout\":250,\"retries\":3}";int r=0;for(int i=0;i<2000000;i++)r=r+cjson_int(j,"port")+cjson_int(j,"timeout");return r&255;}
