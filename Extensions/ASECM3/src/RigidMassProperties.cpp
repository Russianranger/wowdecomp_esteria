#include "ASECM3/RigidMassProperties.h"
#include "btBulletDynamicsCommon.h"
#include <cmath>
namespace ASECM3 {
RigidMassProperties RecenterRigidCompound(btCompoundShape& shape,const std::vector<double>& masses){
    if(masses.empty()||masses.size()>16384||masses.size()!=size_t(shape.getNumChildShapes()))throw Error("Rigid compound child/mass counts disagree or exceed budget");
    RigidMassProperties out;std::vector<btScalar> weights;weights.reserve(masses.size());
    for(double mass:masses){if(!std::isfinite(mass)||mass<=1e-12||mass>1e9)throw Error("Rigid compound child mass is outside finite limits");out.mass+=mass;if(out.mass>1e9)throw Error("Rigid compound total mass exceeds limit");weights.push_back(btScalar(mass));}
    btTransform principal;principal.setIdentity();btVector3 inertia;shape.calculatePrincipalAxisTransform(weights.data(),principal,inertia);
    for(unsigned axis=0;axis<3;++axis)if(!std::isfinite(double(inertia[axis]))||inertia[axis]<=0||!std::isfinite(double(principal.getOrigin()[axis])))throw Error("Rigid compound mass frame or inertia is invalid");
    out.centerInBone=Matrix::Identity();for(unsigned row=0;row<3;++row){for(unsigned col=0;col<3;++col){const double value=principal.getBasis()[row][col];if(!std::isfinite(value))throw Error("Rigid compound principal basis is invalid");out.centerInBone.At(row,col)=value;}out.centerInBone.At(row,3)=principal.getOrigin()[row];}
    out.inertia={float(inertia.x()),float(inertia.y()),float(inertia.z())};
    const auto inverse=principal.inverse();for(int i=0;i<shape.getNumChildShapes();++i)shape.updateChildTransform(i,inverse*shape.getChildTransform(i),false);shape.recalculateLocalAabb();return out;
}
}
