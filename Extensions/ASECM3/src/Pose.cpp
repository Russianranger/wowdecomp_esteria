#include "ASECM3/Pose.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace ASECM3 {
Matrix Matrix::Identity() { Matrix out; for (unsigned i=0;i<4;++i) out.At(i,i)=1; return out; }
Matrix Multiply(const Matrix& a,const Matrix& b) {
    Matrix out;
    for (unsigned r=0;r<4;++r) for (unsigned c=0;c<4;++c) {
        for (unsigned k=0;k<4;++k) out.At(r,c)+=a.At(r,k)*b.At(k,c);
        if (!std::isfinite(out.At(r,c))) throw Error("Non-finite M3 matrix product");
    }
    return out;
}
Matrix InverseAffine(const Matrix& m) {
    if (std::abs(m.At(3,0))+std::abs(m.At(3,1))+std::abs(m.At(3,2))>1e-8 || std::abs(m.At(3,3)-1)>1e-8)
        throw Error("M3 inverse requested for a non-affine matrix");
    const double a=m.At(0,0),b=m.At(0,1),c=m.At(0,2),d=m.At(1,0),e=m.At(1,1),f=m.At(1,2),g=m.At(2,0),h=m.At(2,1),i=m.At(2,2);
    const double determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if (!std::isfinite(determinant) || std::abs(determinant)<1e-12) throw Error("M3 affine matrix is singular");
    Matrix out=Matrix::Identity();
    out.At(0,0)=(e*i-f*h)/determinant; out.At(0,1)=(c*h-b*i)/determinant; out.At(0,2)=(b*f-c*e)/determinant;
    out.At(1,0)=(f*g-d*i)/determinant; out.At(1,1)=(a*i-c*g)/determinant; out.At(1,2)=(c*d-a*f)/determinant;
    out.At(2,0)=(d*h-e*g)/determinant; out.At(2,1)=(b*g-a*h)/determinant; out.At(2,2)=(a*e-b*d)/determinant;
    for (unsigned r=0;r<3;++r) out.At(r,3)=-(out.At(r,0)*m.At(0,3)+out.At(r,1)*m.At(1,3)+out.At(r,2)*m.At(2,3));
    return out;
}
Matrix Compose(const KeyValue& t,const KeyValue& q,const KeyValue& s) {
    double norm=0; for (auto v:q) norm+=double(v)*v;
    if (!std::isfinite(norm) || norm<1e-16) throw Error("M3 pose has a zero quaternion");
    const double inv=1/std::sqrt(norm),x=q[0]*inv,y=q[1]*inv,z=q[2]*inv,w=q[3]*inv;
    Matrix out=Matrix::Identity();
    out.At(0,0)=1-2*(y*y+z*z); out.At(0,1)=2*(x*y-z*w); out.At(0,2)=2*(x*z+y*w);
    out.At(1,0)=2*(x*y+z*w); out.At(1,1)=1-2*(x*x+z*z); out.At(1,2)=2*(y*z-x*w);
    out.At(2,0)=2*(x*z-y*w); out.At(2,1)=2*(y*z+x*w); out.At(2,2)=1-2*(x*x+y*y);
    for (unsigned r=0;r<3;++r) { for (unsigned c=0;c<3;++c) out.At(r,c)*=s[c]; out.At(r,3)=t[r]; }
    for (auto value:out.values) if (!std::isfinite(value)) throw Error("M3 pose contains non-finite values");
    return out;
}
Vec3 TransformPoint(const Matrix& m,const Vec3& p) {
    const double v[3]={p.x,p.y,p.z}; double out[3]={m.At(0,3),m.At(1,3),m.At(2,3)};
    for (unsigned r=0;r<3;++r) for (unsigned c=0;c<3;++c) out[r]+=m.At(r,c)*v[c];
    for (auto n:out) if (!std::isfinite(n) || std::abs(n)>std::numeric_limits<float>::max()) throw Error("M3 point transform is outside finite float range");
    return {static_cast<float>(out[0]),static_cast<float>(out[1]),static_cast<float>(out[2])};
}
Pose EvaluatePoseAtSourceTime(const ModelData& model,const AnimationBank& bank,int32_t group,int32_t time) {
    if (group<-1 || (group>=0 && size_t(group)>=bank.groups.size())) throw Error("M3 pose group is unavailable");
    struct Choice { const TransformTrack* track; uint32_t priority; };
    std::unordered_map<uint64_t,Choice> selected;
    if (group>=0) for (auto index:bank.groups[group].collections) {
        if (index>=bank.collections.size()) throw Error("M3 pose collection is unavailable");
        const auto& collection=bank.collections[index];
        for (const auto& entry:collection.transforms) {
            auto prior=selected.find(entry.first);
            if (prior==selected.end() || collection.priority>=prior->second.priority) selected[entry.first]={&entry.second,collection.priority};
        }
    }
    auto sample=[&](const AnimatedValue& reference,uint32_t type) {
        KeyValue fallback{}; std::copy(reference.value,reference.value+4,fallback.begin());
        const auto found=selected.find((uint64_t(type)<<32)|reference.id);
        return found==selected.end()?fallback:SampleTransform(*found->second.track,time,reference.interpolation,fallback);
    };
    std::vector<LocalTransform> local;local.reserve(model.skeleton.size());
    for(const auto& bone:model.skeleton)local.push_back({sample(bone.translation,2),sample(bone.rotation,3),sample(bone.scale,2)});
    auto out=PoseFromLocalTransforms(model,std::move(local));
    if(group<0)for(const auto& skin:out.skin)for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
        out.maximumDefaultBindError=std::max(out.maximumDefaultBindError,std::abs(skin.At(r,c)-(r==c?1:0)));
    return out;
}
Pose PoseFromLocalTransforms(const ModelData& model,std::vector<LocalTransform> local){
    const auto count=model.skeleton.size(); if(local.size()!=count)throw Error("M3 local pose does not match skeleton");
    Pose out;out.local=std::move(local); out.world.resize(count); out.skin.resize(count); out.normal.resize(count);
    std::vector<uint8_t> state(count,0);
    auto evaluate=[&](size_t index) {
        const auto& bone=model.skeleton[index];
        // These flags are absent in the supplied corpus. Their semantics must
        // be established rather than silently treating selective inheritance
        // as ordinary parent multiplication.
        if (bone.flags&7) throw Error("M3 selective bone inheritance is not implemented: "+bone.name);
        const auto& transform=out.local[index];
        Matrix local=Compose(transform.translation,transform.rotation,transform.scale);
        if (bone.parent>=0) {
            if (size_t(bone.parent)>=count) throw Error("M3 pose parent is outside skeleton");
            if (state[bone.parent]!=2) throw Error("M3 pose parent was not evaluated");
            out.world[index]=Multiply(out.world[bone.parent],local);
        } else out.world[index]=local;
        Matrix inverseBind; for (unsigned j=0;j<16;++j) inverseBind.values[j]=bone.inverseBind[j];
        out.skin[index]=Multiply(out.world[index],inverseBind);
        // Inverse-transpose normals retain nonuniform and reflected scale.
        // Zero scale can intentionally hide geometry; its normals remain zero.
        const auto& skin=out.skin[index];
        const double determinant=skin.At(0,0)*(skin.At(1,1)*skin.At(2,2)-skin.At(1,2)*skin.At(2,1))-skin.At(0,1)*(skin.At(1,0)*skin.At(2,2)-skin.At(1,2)*skin.At(2,0))+skin.At(0,2)*(skin.At(1,0)*skin.At(2,1)-skin.At(1,1)*skin.At(2,0));
        if (std::abs(determinant)>=1e-12) {
            const auto inverse=InverseAffine(skin);
            for (unsigned r=0;r<3;++r) for (unsigned c=0;c<3;++c) out.normal[index].At(r,c)=inverse.At(c,r);
        }
        state[index]=2;
    };
    std::vector<size_t> chain;
    for (size_t i=0;i<count;++i) {
        if (state[i]==2) continue;
        chain.clear(); int64_t cursor=static_cast<int64_t>(i);
        while (cursor>=0) {
            if (static_cast<uint64_t>(cursor)>=count) throw Error("M3 pose parent is outside skeleton");
            if (state[cursor]==2) break;
            if (state[cursor]==1) throw Error("M3 pose hierarchy contains a cycle");
            state[cursor]=1; chain.push_back(static_cast<size_t>(cursor));
            cursor=model.skeleton[cursor].parent;
            if (cursor<-1) throw Error("M3 pose parent is invalid");
        }
        for (auto it=chain.rbegin();it!=chain.rend();++it) evaluate(*it);
    }
    return out;
}
Pose BlendLocalPoses(const ModelData& model,const std::vector<LocalTransform>& from,Pose target,double amount){
    if(!std::isfinite(amount)||amount<0||amount>1)throw Error("M3 transition weight must be in 0..1");
    if(from.size()!=model.skeleton.size()||target.local.size()!=from.size()||target.world.size()!=from.size()||target.skin.size()!=from.size()||target.normal.size()!=from.size())throw Error("M3 transition pose does not match skeleton");
    if(amount==1)return target;
    if(amount==0)return PoseFromLocalTransforms(model,from);
    auto local=std::move(target.local);
    auto normalize=[](KeyValue q){double length=0;for(auto v:q)length+=double(v)*v;if(!std::isfinite(length)||length<1e-16)throw Error("M3 transition quaternion is invalid");for(auto& v:q)v=float(v/std::sqrt(length));return q;};
    for(size_t i=0;i<local.size();++i){
        auto& value=local[i];const auto& prior=from[i];
        for(unsigned c=0;c<3;++c){value.translation[c]=float(prior.translation[c]+(value.translation[c]-prior.translation[c])*amount);value.scale[c]=float(prior.scale[c]+(value.scale[c]-prior.scale[c])*amount);}
        auto a=normalize(prior.rotation),b=normalize(value.rotation);double dot=0;for(unsigned c=0;c<4;++c)dot+=double(a[c])*b[c];
        if(dot<0){dot=-dot;for(auto& v:b)v=-v;}
        dot=std::clamp(dot,0.0,1.0);double wa=1-amount,wb=amount;
        if(dot<.9995){const auto angle=std::acos(dot),sine=std::sin(angle);wa=std::sin((1-amount)*angle)/sine;wb=std::sin(amount*angle)/sine;}
        for(unsigned c=0;c<4;++c)value.rotation[c]=float(a[c]*wa+b[c]*wb);
        value.rotation=normalize(value.rotation);
    }
    return PoseFromLocalTransforms(model,std::move(local));
}

std::vector<SkinnedRegion> SkinModel(const ModelData& model,const Pose& pose) {
    if (pose.skin.size()!=model.skeleton.size() || pose.normal.size()!=model.skeleton.size()) throw Error("M3 skinning pose does not match its skeleton");
    std::vector<SkinnedRegion> out;
    for (uint32_t d=0;d<model.divisions.size();++d) for (uint32_t r=0;r<model.divisions[d].regions.size();++r) {
        const auto& region=model.divisions[d].regions[r];
        if (uint64_t(region.firstVertex)+region.vertexCount>model.vertices.size() || uint64_t(region.firstBoneLookup)+region.boneLookupCount>model.bonePalette.size()) throw Error("M3 skinning region range is invalid");
        SkinnedRegion result; result.division=d; result.region=r; result.positions.reserve(region.vertexCount); result.normals.reserve(region.vertexCount);
        result.tangents.reserve(region.vertexCount);result.tangentSigns.reserve(region.vertexCount);
        for (size_t v=region.firstVertex;v<size_t(region.firstVertex)+region.vertexCount;++v) {
            const auto& vertex=model.vertices[v]; Vec3 position{},normal{},tangent{},bitangent{}; unsigned total=0;
            const float sourceSign=vertex.tangentSign<128?-1.0f:1.0f;
            const Vec3 sourceB{sourceSign*(vertex.normal.y*vertex.tangent.z-vertex.normal.z*vertex.tangent.y),sourceSign*(vertex.normal.z*vertex.tangent.x-vertex.normal.x*vertex.tangent.z),sourceSign*(vertex.normal.x*vertex.tangent.y-vertex.normal.y*vertex.tangent.x)};
            for (unsigned w:vertex.weights) total+=w;
            if (!total) { position=vertex.position; normal=vertex.normal;tangent=vertex.tangent;bitangent=sourceB; }
            else for (unsigned i=0;i<4;++i) if (vertex.weights[i]) {
                if (vertex.boneLookup[i]>=region.boneLookupCount) throw Error("M3 skinning local palette index is invalid");
                const auto bone=model.bonePalette[region.firstBoneLookup+vertex.boneLookup[i]];
                if (bone>=pose.skin.size()) throw Error("M3 skinning bone index is invalid");
                const float weight=float(vertex.weights[i])/total;
                const auto p=TransformPoint(pose.skin[bone],vertex.position),n=TransformPoint(pose.normal[bone],vertex.normal);
                position.x+=weight*p.x; position.y+=weight*p.y; position.z+=weight*p.z;
                normal.x+=weight*n.x; normal.y+=weight*n.y; normal.z+=weight*n.z;
                // Tangent directions use the forward linear skin matrix;
                // normals use its inverse transpose. Never translate a basis.
                auto direction=[&](const Vec3& v){const auto& a=pose.skin[bone];return Vec3{float(a.At(0,0)*v.x+a.At(0,1)*v.y+a.At(0,2)*v.z),float(a.At(1,0)*v.x+a.At(1,1)*v.y+a.At(1,2)*v.z),float(a.At(2,0)*v.x+a.At(2,1)*v.y+a.At(2,2)*v.z)};};
                const auto t=direction(vertex.tangent),b=direction(sourceB);
                tangent.x+=weight*t.x;tangent.y+=weight*t.y;tangent.z+=weight*t.z;
                bitangent.x+=weight*b.x;bitangent.y+=weight*b.y;bitangent.z+=weight*b.z;
            }
            const double length=std::sqrt(double(normal.x)*normal.x+double(normal.y)*normal.y+double(normal.z)*normal.z);
            if (length>1e-12) {normal.x=static_cast<float>(normal.x/length);normal.y=static_cast<float>(normal.y/length);normal.z=static_cast<float>(normal.z/length);}
            const double dot=double(normal.x)*tangent.x+double(normal.y)*tangent.y+double(normal.z)*tangent.z;
            tangent.x-=float(dot*normal.x);tangent.y-=float(dot*normal.y);tangent.z-=float(dot*normal.z);
            const double tl=std::sqrt(double(tangent.x)*tangent.x+double(tangent.y)*tangent.y+double(tangent.z)*tangent.z);
            float sign=0;
            if(vertex.hasTangent&&tl>1e-12&&length>1e-12){tangent.x=float(tangent.x/tl);tangent.y=float(tangent.y/tl);tangent.z=float(tangent.z/tl);
                const double handed=(double(normal.y)*tangent.z-double(normal.z)*tangent.y)*bitangent.x+(double(normal.z)*tangent.x-double(normal.x)*tangent.z)*bitangent.y+(double(normal.x)*tangent.y-double(normal.y)*tangent.x)*bitangent.z;
                if(vertex.hasTangentSign&&std::abs(handed)>1e-12)sign=handed<0?-1.0f:1.0f;
            }else tangent={};
            if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) throw Error("M3 skinning produced a non-finite vertex");
            if(!std::isfinite(tangent.x)||!std::isfinite(tangent.y)||!std::isfinite(tangent.z))throw Error("M3 skinning produced a non-finite tangent");
            result.positions.push_back(position); result.normals.push_back(normal);
            result.tangents.push_back(tangent);result.tangentSigns.push_back(sign);
        }
        out.push_back(std::move(result));
    }
    return out;
}
}
