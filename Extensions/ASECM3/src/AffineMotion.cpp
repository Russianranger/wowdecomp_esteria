#include "ASECM3/AffineMotion.h"
#include "LinearMath/btPolarDecomposition.h"
#include <algorithm>
#include <cmath>
namespace ASECM3 {
AffineMotion::Parts AffineMotion::Decode(const Matrix& matrix){
    for(double value:matrix.values)if(!std::isfinite(value)||std::abs(value)>1e6)throw Error("Affine motion endpoint exceeds finite bounds");
    if(std::abs(matrix.At(3,0))+std::abs(matrix.At(3,1))+std::abs(matrix.At(3,2))+std::abs(matrix.At(3,3)-1)>1e-9)throw Error("Affine motion endpoint is not affine");
    double maximum=0;for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)maximum=std::max(maximum,std::abs(matrix.At(r,c)));
    if(maximum<1e-12)throw Error("Affine motion endpoint has zero scale");
    btMatrix3x3 input;for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)input[r][c]=btScalar(matrix.At(r,c)/maximum);
    const auto det=input.determinant();if(!std::isfinite(double(det))||btFabs(det)<1e-10)throw Error("Affine motion endpoint is singular or ill-conditioned");
    Parts out;out.sign=det<0?-1:1;input=input*btScalar(out.sign);
    btMatrix3x3 rotation,stretch;const btPolarDecomposition polar(btScalar(1e-6),32);
    // PHCC frames are usually rotation plus axis scale. Recognize that case
    // directly instead of performing iterative polar inversions twice for
    // every capsule on every animation update. Sheared frames retain polar.
    const auto x=input.getColumn(0),y=input.getColumn(1),z=input.getColumn(2);
    const btScalar sx=x.length(),sy=y.length(),sz=z.length();
    if(btFabs(x.dot(y))<=btScalar(1e-6)*sx*sy&&btFabs(x.dot(z))<=btScalar(1e-6)*sx*sz&&btFabs(y.dot(z))<=btScalar(1e-6)*sy*sz){
        for(unsigned r=0;r<3;++r){rotation[r][0]=input[r][0]/sx;rotation[r][1]=input[r][1]/sy;rotation[r][2]=input[r][2]/sz;}
        stretch=btMatrix3x3(sx,0,0,0,sy,0,0,0,sz);
    }else if(polar.decompose(input,rotation,stretch)==polar.maxIterations())throw Error("Affine motion polar decomposition did not converge");
    const auto reconstructed=rotation*stretch,orthogonal=rotation.transpose()*rotation;
    double residual=0,error=0;
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c){
        if(!std::isfinite(double(rotation[r][c]))||!std::isfinite(double(stretch[r][c])))throw Error("Affine motion decomposition is not finite");
        residual=std::max(residual,std::abs(double(reconstructed[r][c]-input[r][c])));
        error=std::max(error,std::abs(double(orthogonal[r][c])-(r==c?1.0:0.0)));
    }
    // A convex interpolation of positive-definite stretches stays invertible.
    if(!std::isfinite(residual)||!std::isfinite(error)||residual>1e-4||error>1e-4||rotation.determinant()<.9999f||stretch[0][0]<=0||stretch[0][0]*stretch[1][1]-stretch[0][1]*stretch[1][0]<=0||stretch.determinant()<=0)throw Error("Affine motion decomposition failed reconstruction or positive-stretch checks");
    btQuaternion quaternion;rotation.getRotation(quaternion);double norm=0;
    for(unsigned i=0;i<4;++i){out.rotation[i]=quaternion[i];norm+=out.rotation[i]*out.rotation[i];}
    if(!std::isfinite(norm)||norm<1e-12)throw Error("Affine motion rotation is invalid");
    for(auto& value:out.rotation)value/=std::sqrt(norm);
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)out.stretch[r*3+c]=double(stretch[r][c])*maximum*out.sign;
    return out;
}
AffineMotion::AffineMotion(const Matrix& first,const Matrix& last):m_first(first),m_last(last),m_from(Decode(first)),m_to(Decode(last)){
    if(m_from.sign!=m_to.sign)throw Error("Affine motion changes handedness inside observed interval");
}
Matrix AffineMotion::At(double weight)const{
    if(!std::isfinite(weight)||weight<0||weight>1)throw Error("Affine motion weight is outside [0,1]");
    if(weight==0)return m_first;if(weight==1)return m_last;
    double dot=0;for(unsigned i=0;i<4;++i)dot+=m_from.rotation[i]*m_to.rotation[i];
    const double sign=dot<0?-1:1;dot=std::clamp(std::abs(dot),0.0,1.0);
    double a=1-weight,b=weight;
    if(dot<.9995){const double angle=std::acos(dot),sine=std::sin(angle);a=std::sin((1-weight)*angle)/sine;b=std::sin(weight*angle)/sine;}
    double q[4],norm=0;for(unsigned i=0;i<4;++i){q[i]=a*m_from.rotation[i]+b*sign*m_to.rotation[i];norm+=q[i]*q[i];}
    if(!std::isfinite(norm)||norm<1e-12)throw Error("Affine motion interpolated rotation is invalid");
    for(auto& value:q)value/=std::sqrt(norm);
    const double x=q[0],y=q[1],z=q[2],w=q[3];
    const double rotation[3][3]={{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},{2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},{2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}};
    Matrix result=Matrix::Identity();
    for(unsigned r=0;r<3;++r){
        result.At(r,3)=m_first.At(r,3)*(1-weight)+m_last.At(r,3)*weight;
        for(unsigned c=0;c<3;++c){result.At(r,c)=0;for(unsigned k=0;k<3;++k)result.At(r,c)+=rotation[r][k]*(m_from.stretch[k*3+c]*(1-weight)+m_to.stretch[k*3+c]*weight);}
    }
    for(auto value:result.values)if(!std::isfinite(value))throw Error("Affine motion result is not finite");
    return result;
}
}
