// Local geometric completion helper. All results require KiCad DRC validation.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <queue>
#include <vector>
struct Entry { float score; int id; bool operator<(const Entry& o)const{return score>o.score;} };
int main(int argc,char**argv){
 if(argc!=3)return 2;
 std::ifstream f(argv[1],std::ios::binary);int32_t d[3];f.read((char*)d,12);
 int nx=d[0],ny=d[1],nl=d[2],plane=nx*ny,total=plane*nl;
 std::vector<uint8_t> blocked(total),via(plane),start(total),goal(total);
 f.read((char*)blocked.data(),total);f.read((char*)via.data(),plane);f.read((char*)start.data(),total);f.read((char*)goal.data(),total);
 int minx=nx,maxx=0,miny=ny,maxy=0;bool foundgoal=false;
 for(int i=0;i<total;i++)if(goal[i]&&!blocked[i]){int v=i%plane;minx=std::min(minx,v%nx);maxx=std::max(maxx,v%nx);miny=std::min(miny,v/nx);maxy=std::max(maxy,v/nx);foundgoal=true;}
 if(!foundgoal)return 3;
 auto heuristic=[&](int id){int p=id%plane,x=p%nx,y=p/nx;int dx=std::max({minx-x,0,x-maxx}),dy=std::max({miny-y,0,y-maxy});return float(std::max(dx,dy)+.41421356237*std::min(dx,dy));};
 std::vector<float> dist(total,std::numeric_limits<float>::infinity());std::vector<int> prev(total,-1);std::priority_queue<Entry> q;
 for(int i=0;i<total;i++)if(start[i]&&!blocked[i]){dist[i]=0;prev[i]=-2;q.push({heuristic(i),i});}
 int last=-1,expanded=0;const int dx[]={1,1,0,-1,-1,-1,0,1},dy[]={0,1,1,1,0,-1,-1,-1};
 while(!q.empty()){
  Entry e=q.top();q.pop();int u=e.id;if(e.score>dist[u]+heuristic(u)+.001)continue;
  if(goal[u]){last=u;break;}if(++expanded>8000000)break;
  int p=u%plane,x=p%nx,y=p/nx,z=u/plane;
  auto relax=[&](int v,float cost){float nd=dist[u]+cost;if(nd+.0001<dist[v]){dist[v]=nd;prev[v]=u;q.push({nd+heuristic(v),v});}};
  for(int m=0;m<8;m++){int xx=x+dx[m],yy=y+dy[m];if(xx<0||xx>=nx||yy<0||yy>=ny)continue;int v=z*plane+yy*nx+xx;if(blocked[v])continue;if(dx[m]&&dy[m]&&(blocked[z*plane+y*nx+xx]||blocked[z*plane+yy*nx+x]))continue;relax(v,m%2?1.41421356237f:1.f);}
  if(!via[p])for(int zz=0;zz<nl;zz++)if(zz!=z&&!blocked[zz*plane+p])relax(zz*plane+p,100.f);
 }
 if(last<0){std::fprintf(stderr,"No path (%d expanded)\n",expanded);return 4;}
 std::vector<int32_t> path;for(int p=last;p!=-2;p=prev[p])path.push_back(p);std::reverse(path.begin(),path.end());
 std::ofstream out(argv[2],std::ios::binary);out.write((char*)path.data(),path.size()*4);std::fprintf(stderr,"Path %zu states, %d expanded\n",path.size(),expanded);
}
