int main(void){
  double a[1024],b[1024];
  for(int i=0;i<1024;i++){a[i]=(double)((i&7)+1);b[i]=(double)((i&3)+1);}
  double acc=0.0;
  for(int r=0;r<20000;r++){
    a[r&1023]=(double)((r&7)+1);
    double s=0.0;
    for(int i=0;i<1024;i++) s+=a[i]*b[i];
    acc+=s;
  }
  return (int)acc & 255;
}
