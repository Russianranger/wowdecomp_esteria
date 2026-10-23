#include "ASECM3/Physics.h"
#include <algorithm>
#include <unordered_map>
namespace ASECM3 {
namespace {
Matrix ReadMatrix(const Record& r,const std::string& name){
    Matrix out;
    for(unsigned c=0;c<4;++c)for(unsigned row=0;row<4;++row)
        out.At(row,c)=r.Float(name+"."+"xyzw"[c]+"."+"xyzw"[row]);
    return out;
}
Reference Optional(const Record& r,const char* name){return r.Has(name)?r.Ref(name):Reference{};}
void Budget(const Reference& r,uint32_t limit,const char* name){if(r.count>limit)throw Error(std::string("M3 physics ")+name+" exceeds its decode budget");}
void Charge(size_t bytes,size_t& owned){if(bytes>64u*1024u*1024u-owned)throw Error("M3 decoded physics exceeds 64 MiB");owned+=bytes;}
template<class T>std::vector<T> Integers(const Document& doc,const Reference& ref,const char* tag,size_t& owned){
    Budget(ref,3000000,"integer payload");std::vector<T> out;if(!ref.count)return out;
    Charge(size_t(ref.count)*sizeof(T),owned);
    const auto& s=doc.Resolve(ref,tag);out.reserve(ref.count);
    for(uint32_t i=0;i<ref.count;++i){const size_t p=s.offset+size_t(i)*sizeof(T);
        if constexpr(sizeof(T)==1)out.push_back(doc.U8(p));
        else if constexpr(sizeof(T)==2)out.push_back(doc.U16(p));
        else if constexpr(sizeof(T)==4)out.push_back(doc.U32(p));
        else out.push_back(uint64_t(doc.U32(p))|(uint64_t(doc.U32(p+4))<<32));
    }return out;
}
}
PhysicsData DecodePhysics(const Document& doc,const ModelData& model){
    PhysicsData out;const auto root=doc.Model();
    const auto bodies=Optional(root,"physics_rigidbodies"),joints=Optional(root,"physics_joints"),cloth=Optional(root,"physics_cloths"),constraints=Optional(root,"physics_constraints");
    Budget(bodies,4096,"body count");Budget(joints,16384,"joint count");Budget(cloth,4096,"cloth count");Budget(constraints,16384,"constraint count");
    out.clothCount=cloth.count;out.constraintCount=constraints.count;
    // Follow only the physics roots and their own reference graph. The global
    // section index is not a proxy for ownership (it also contains mesh/FX).
    std::vector<Reference> pending{bodies,joints,cloth,constraints};
    std::unordered_map<uint32_t,uint32_t> visited;size_t retained=0;
    while(!pending.empty()){
        const auto ref=pending.back();pending.pop_back();if(!ref.count)continue;
        const auto& section=doc.Resolve(ref);
        auto found=visited.find(ref.section);
        if(found!=visited.end()&&found->second>=ref.count)continue;
        const uint32_t first=found==visited.end()?0:found->second;
        if(!section.layout)throw Error("M3 physics references an unknown payload layout: "+section.tag);
        if(visited.size()>=65536&&found==visited.end())throw Error("M3 physics reference graph exceeds its section budget");
        const size_t size=size_t(ref.count)*section.layout->size;
        const size_t extra=size_t(ref.count-first)*section.layout->size;
        if(extra>64u*1024u*1024u-retained)throw Error("M3 physics snapshots exceed 64 MiB");
        retained+=extra;
        auto payload=std::find_if(out.payloads.begin(),out.payloads.end(),[&](const auto& p){return p.section==ref.section;});
        if(payload==out.payloads.end()){out.payloads.push_back({ref.section,ref.count,section.version,section.tag,{}});payload=out.payloads.end()-1;}
        const auto* bytes=doc.Data(section.offset,size);payload->bytes.assign(bytes,bytes+size);payload->count=ref.count;visited[ref.section]=ref.count;
        for(uint32_t i=first;i<ref.count;++i){const auto r=doc.At(ref,i);for(uint32_t f=0;f<r.Schema().fieldCount;++f){const auto& field=r.Schema().fields[f];if(std::string_view(field.kind)=="reference")pending.push_back(r.Ref(field.name));}}
    }
    size_t decoded=0,totalShapes=0;
    for(uint32_t i=0;i<bodies.count;++i){
        const auto r=doc.At(bodies,i);PhysicsBody body;body.version=r.Schema().version;body.raw=r.Bytes();
        body.bone=r.UInt("bone");if(body.bone>=model.skeleton.size())throw Error("M3 physics body bone is outside the mesh skeleton");
        body.flags=r.UInt("flags");body.priority=r.UInt("priority");body.localForces=r.UInt("local_forces");body.worldForces=r.UInt("world_forces");
        if(r.Has("simulation_type")){
            body.simulationType=r.UInt("simulation_type");body.physicalMaterial=r.UInt("physical_material");
            body.mass=r.Float("mass");body.friction=r.Float("friction");body.bounce=r.Float("bounce");
            body.linearDamping=r.Float("damping_linear");body.angularDamping=r.Float("damping_angular");body.gravityFactor=r.Float("gravity_factor");
        }
        if(r.Has("unknown8de065e8.default")){body.hasActiveState=true;body.activeStateId=r.UInt("unknown8de065e8.header.id");body.activeStateDefault=r.UInt("unknown8de065e8.default");body.activeStateNull=r.UInt("unknown8de065e8.null");body.activeStateInterpolation=r.UInt("unknown8de065e8.header.interpolation");body.activeStateFlags=r.UInt("unknown8de065e8.header.flags");body.blendOut=r.Float("unknown57796a4d");}
        const auto shapes=r.Ref("physics_shape");Budget(shapes,4096,"shapes per body");
        for(uint32_t j=0;j<shapes.count;++j){
            if(++totalShapes>16384)throw Error("M3 total collision shape count exceeds 16384");
            const auto s=doc.At(shapes,j);PhysicsShape shape;shape.version=s.Schema().version;shape.raw=s.Bytes();shape.kind=s.UInt("shape");shape.local=ReadMatrix(s,"matrix");
            for(unsigned k=0;k<3;++k)shape.size[k]=s.Float("size"+std::to_string(k));
            const auto vertices=s.Ref("vertices");Budget(vertices,1000000,"collision vertices");
            Charge(size_t(vertices.count)*sizeof(Vec3),decoded);shape.vertices.reserve(vertices.count);
            for(uint32_t k=0;k<vertices.count;++k){const auto v=doc.At(vertices,k);shape.vertices.push_back({v.Float("x"),v.Float("y"),v.Float("z")});}
            if(s.Has("face_data"))shape.faces=Integers<uint16_t>(doc,s.Ref("face_data"),"U16_",decoded);
            body.shapes.push_back(std::move(shape));
        }
        out.bodies.push_back(std::move(body));
    }
    for(uint32_t i=0;i<joints.count;++i){
        const auto r=doc.At(joints,i);PhysicsJoint joint;joint.raw=r.Bytes();joint.kind=r.UInt("joint_type");
        // The source names these bone1/2. Do not reinterpret them as body IDs
        // or resolve their bind frames until the importer semantics are checked.
        joint.endpoint1=r.UInt("bone1");joint.endpoint2=r.UInt("bone2");joint.frame1=ReadMatrix(r,"matrix1");joint.frame2=ReadMatrix(r,"matrix2");
        joint.limit=r.UInt("limit_bool")!=0;joint.minimum=r.Float("limit_min");joint.maximum=r.Float("limit_max");joint.angle=r.Float("limit_angle");
        joint.frictionEnabled=r.UInt("friction_bool")!=0;joint.friction=r.Float("friction");joint.dampingRatio=r.Float("damping_ratio");joint.angularFrequency=r.Float("angular_frequency");joint.breakThreshold=r.Float("break_threshold");joint.collisionValue=r.Int("shape_collision_value");
        out.joints.push_back(std::move(joint));
    }
    for(uint32_t i=0;i<cloth.count;++i){
        const auto r=doc.At(cloth,i);PhysicsCloth c;c.version=r.Schema().version;c.simulationRegion=r.UInt("simulation_region_index");
        c.skinBones=Integers<uint16_t>(doc,r.Ref("skin_bones"),"U16_",decoded);
        for(auto bone:c.skinBones)if(bone>=model.skeleton.size())throw Error("M3 cloth skin bone is outside its skeleton");
        c.simulated=Integers<uint8_t>(doc,r.Ref("vertex_simulated"),"U8__",decoded);
        c.vertexBones=Integers<uint32_t>(doc,r.Ref("vertex_bones"),"U32_",decoded);c.vertexWeights=Integers<uint32_t>(doc,r.Ref("vertex_weights"),"U32_",decoded);
        if(c.vertexBones.size()!=c.vertexWeights.size()||c.simulated.size()!=c.vertexBones.size())throw Error("M3 cloth per-vertex array counts disagree");
        // Preserve packed bone values without guessing their index domain.
        // Real mesh PHCL records include weighted byte values beyond the
        // skin_bones table; interpreting those bytes as local indices rejects
        // valid supplied models. Bank PHCL records have different contents.
        const auto cc=r.Ref("constraints");Budget(cc,16384,"cloth constraints");
        Charge(size_t(cc.count)*sizeof(ClothConstraint),decoded);
        for(uint32_t k=0;k<cc.count;++k){const auto p=doc.At(cc,k);ClothConstraint constraint;constraint.local=ReadMatrix(p,"matrix");constraint.radius=p.Float("radius");constraint.height=p.Float("height");constraint.bone=p.Int("bone");c.constraints.push_back(std::move(constraint));}
        const auto maps=r.Ref("influence_map");Budget(maps,4096,"cloth influence maps");
        for(uint32_t k=0;k<maps.count;++k){const auto p=doc.At(maps,k);ClothInfluence influence;influence.influencedRegion=p.UInt("influenced_region_index");influence.simulationRegion=p.UInt("simulation_region_index");influence.lookups=Integers<uint64_t>(doc,p.Ref("simulation_vert_lookups"),"U64_",decoded);influence.weights=Integers<uint32_t>(doc,p.Ref("simulation_vert_weights"),"U32_",decoded);if(influence.lookups.size()!=influence.weights.size())throw Error("M3 cloth influence arrays disagree");c.influences.push_back(std::move(influence));}
        for(const auto* name:{"density","tracking","stiffness_stretching","stiffness_horizontal","stiffness_blending","damping","friction","gravity","explosion_scale","wind_scale","stiffness_shear","drag_factor","lift_factor","stiffness_spheres","skin_offset","skin_exponent","skin_stiffness"})if(r.Has(name))c.coefficients.push_back({name,r.Float(name)});
        if(r.Has("local_wind.x")){c.hasLocalWind=true;c.localWind={r.Float("local_wind.x"),r.Float("local_wind.y"),r.Float("local_wind.z")};}
        out.cloths.push_back(std::move(c));
    }
    // Explicit cloth links identify helpers; material/bone names never do.
    // Any ambiguity preserves all geometry and reports why hiding was refused.
    std::vector<uint32_t> helpers,targets;
    auto warn=[&](size_t record,const std::string& reason){if(out.visibilityWarnings.size()<32)out.visibilityWarnings.push_back("PHCL["+std::to_string(record)+"] helper visibility: "+reason+"; geometry preserved");};
    for(size_t record=0;record<out.cloths.size();++record){
        const auto& c=out.cloths[record];if(c.influences.empty())continue;
        if(model.divisions.empty()||c.simulationRegion>=model.divisions[0].regions.size()){warn(record,"simulation region is out of range");continue;}
        const auto& regions=model.divisions[0].regions;const auto& simulation=regions[c.simulationRegion];
        bool valid=true;
        if((simulation.flags&0xDu)!=0x5u){warn(record,"simulation flags disagree with helper role ("+std::to_string(simulation.flags)+")");valid=false;}
        for(const auto& map:c.influences){
            if(map.simulationRegion!=c.simulationRegion||map.influencedRegion>=regions.size()||map.influencedRegion==c.simulationRegion){warn(record,"PHAC region link is invalid");valid=false;continue;}
            const auto& render=regions[map.influencedRegion];targets.push_back(map.influencedRegion);
            if(!(render.flags&0x8u)||(render.flags&0x4u)){warn(record,"render flags disagree with PHAC target role ("+std::to_string(render.flags)+")");valid=false;}
            if(map.lookups.size()!=render.vertexCount||map.weights.size()!=render.vertexCount){warn(record,"PHAC vertex arrays do not match target region");valid=false;continue;}
            for(size_t v=0;v<map.lookups.size();++v)for(unsigned k=0;k<4;++k)
                if(((map.weights[v]>>(k*8))&255u)&&((map.lookups[v]>>(k*16))&65535u)>=simulation.vertexCount){warn(record,"PHAC references a missing simulation vertex");valid=false;break;}
        }
        if(valid)helpers.push_back(c.simulationRegion);
    }
    for(auto helper:helpers)if(std::find(targets.begin(),targets.end(),helper)!=targets.end()){
        out.visibilityWarnings.push_back("PHCL/PHAC region "+std::to_string(helper)+" is both a helper and a render target; geometry preserved");
    }
    if(out.visibilityWarnings.empty()){
        std::sort(helpers.begin(),helpers.end());helpers.erase(std::unique(helpers.begin(),helpers.end()),helpers.end());out.hiddenSimulationRegions=std::move(helpers);
    }
    return out;
}
}
