#include "ASECM3/ParticleTransformInterval.h"
#include <algorithm>
#include <cmath>
namespace ASECM3 {
ParticleTransformInterval::Transform ParticleTransformInterval::Decode(const Matrix& matrix){
    Transform out;out.scale=ParticleUniformScale(matrix);
    const double det=matrix.At(0,0)*(matrix.At(1,1)*matrix.At(2,2)-matrix.At(1,2)*matrix.At(2,1))-matrix.At(0,1)*(matrix.At(1,0)*matrix.At(2,2)-matrix.At(1,2)*matrix.At(2,0))+matrix.At(0,2)*(matrix.At(1,0)*matrix.At(2,1)-matrix.At(1,1)*matrix.At(2,0));
    if(det<0)out.scale=-out.scale;
    double r[3][3];for(unsigned i=0;i<3;++i){out.translation[i]=float(matrix.At(i,3));if(!std::isfinite(out.translation[i]))throw Error("M3 particle transform translation exceeds float range");for(unsigned j=0;j<3;++j)r[i][j]=matrix.At(i,j)/out.scale;}
    double q[4];const double trace=r[0][0]+r[1][1]+r[2][2];
    if(trace>0){const double s=2*std::sqrt(trace+1);q[3]=s*.25;q[0]=(r[2][1]-r[1][2])/s;q[1]=(r[0][2]-r[2][0])/s;q[2]=(r[1][0]-r[0][1])/s;}
    else{unsigned i=0;if(r[1][1]>r[i][i])i=1;if(r[2][2]>r[i][i])i=2;const unsigned j=(i+1)%3,k=(i+2)%3;const double s=2*std::sqrt(std::max(0.0,1+r[i][i]-r[j][j]-r[k][k]));if(s<1e-12)throw Error("M3 particle rotation cannot be decomposed");q[i]=s*.25;q[j]=(r[j][i]+r[i][j])/s;q[k]=(r[k][i]+r[i][k])/s;q[3]=(r[k][j]-r[j][k])/s;}
    double norm=0;for(double value:q)norm+=value*value;if(!std::isfinite(norm)||norm<1e-16)throw Error("Invalid M3 particle actor orientation");for(unsigned i=0;i<4;++i)out.rotation[i]=float(q[i]/std::sqrt(norm));return out;
}
ParticleTransformInterval::ParticleTransformInterval(double begin,double end,const Matrix& first,const Matrix& last)
    :m_begin(begin),m_end(end),m_first(first),m_last(last),m_from(Decode(first)),m_to(Decode(last)){
    if(!std::isfinite(begin)||!std::isfinite(end)||begin<0||end<begin)throw Error("Invalid M3 particle transform interval");
    if((m_from.scale<0)!=(m_to.scale<0))throw Error("M3 particle actor transform changes handedness within an interval");
}
Matrix ParticleTransformInterval::At(double time)const{
    if(!std::isfinite(time)||time<m_begin||time>m_end)throw Error("M3 particle birth is outside observed actor history");
    if(time==m_end)return m_last;if(time==m_begin)return m_first;
    const double weight=(time-m_begin)/(m_end-m_begin);double dot=0;for(unsigned i=0;i<4;++i)dot+=double(m_from.rotation[i])*m_to.rotation[i];
    auto to=m_to.rotation;if(dot<0){dot=-dot;for(auto& value:to)value=-value;}dot=std::clamp(dot,0.0,1.0);
    double a=1-weight,b=weight;if(dot<.9995){const double angle=std::acos(dot),sine=std::sin(angle);a=std::sin(a*angle)/sine;b=std::sin(b*angle)/sine;}
    KeyValue translation{},rotation{};for(unsigned i=0;i<3;++i)translation[i]=float(double(m_from.translation[i])*(1-weight)+double(m_to.translation[i])*weight);for(unsigned i=0;i<4;++i)rotation[i]=float(m_from.rotation[i]*a+to[i]*b);
    const float scale=float(m_from.scale*(1-weight)+m_to.scale*weight);return Compose(translation,rotation,{scale,scale,scale,0});
}
}
