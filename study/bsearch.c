static int bsearch2(int*a,int n,int key){int lo=0,hi=n-1;while(lo<=hi){int m=(lo+hi)/2;if(a[m]==key)return m;if(a[m]<key)lo=m+1;else hi=m-1;}return -1;}
int main(){int a[6]={1,3,5,7,9,11};if(bsearch2(a,6,7)!=3)return 1;if(bsearch2(a,6,4)!=-1)return 2;return 0;}
