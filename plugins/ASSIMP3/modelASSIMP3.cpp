////////////////////////////////////////////////////////
//
// GEM - Graphics Environment for Multimedia
//
// zmoelnig@iem.at
//
// Implementation file
//
//    Copyright (c) 2001-2014 IOhannes m zmölnig. forum::für::umläute. IEM. zmoelnig@iem.at
//    For information on usage and redistribution, and for a DISCLAIMER OF ALL
//    WARRANTIES, see the file, "GEM.LICENSE.TERMS" in this distribution.
//
/////////////////////////////////////////////////////////
#include "modelASSIMP3.h"
#include "plugins/PluginFactory.h"
#include "Gem/RTE.h"
#include "Gem/Exception.h"
#include "Gem/Properties.h"
#include "Gem/VertexBuffer.h"
#include "Gem/GemGL.h"

#include "Utils/Functions.h"

#include <string>

using namespace gem::plugins;

REGISTER_MODELLOADERFACTORY("ASSIMP3", modelASSIMP3);

namespace
{
#define aisgl_min(x,y) (x<y?x:y)
#define aisgl_max(x,y) (y>x?y:x)

static void post_meshdata(gem::plugins::modelASSIMP3::meshdata&meshdata) {
  gem::plugins::modelloader::mesh&mesh = meshdata.mesh;
  post("MESHDATA @%p", &meshdata);
  post("    MESH @%p [%d]", &mesh, mesh.size);
  post("          vertices=%p", mesh.vertices);
  post("           normals=%p", mesh.normals);
  post("            colors=%p", mesh.colors);
  post("         texcoords=%p", mesh.texcoords);
}

// ----------------------------------------------------------------------------
static void get_bounding_box_for_node (const struct aiScene*scene,
                                       const struct aiNode* nd,
                                       aiVector3D* min,
                                       aiVector3D* max,
                                       aiMatrix4x4* trafo
                                      )
{
  aiMatrix4x4 prev;
  unsigned int n = 0, t;

  prev = *trafo;
  aiMultiplyMatrix4(trafo,&nd->mTransformation);

  for (; n < nd->mNumMeshes; ++n) {
    const struct aiMesh* mesh = scene->mMeshes[nd->mMeshes[n]];
    for (t = 0; t < mesh->mNumVertices; ++t) {

      aiVector3D tmp = mesh->mVertices[t];
      aiTransformVecByMatrix4(&tmp,trafo);

      min->x = aisgl_min(min->x,tmp.x);
      min->y = aisgl_min(min->y,tmp.y);
      min->z = aisgl_min(min->z,tmp.z);

      max->x = aisgl_max(max->x,tmp.x);
      max->y = aisgl_max(max->y,tmp.y);
      max->z = aisgl_max(max->z,tmp.z);
    }
  }

  for (n = 0; n < nd->mNumChildren; ++n) {
    get_bounding_box_for_node(scene, nd->mChildren[n],min,max,trafo);
  }
  *trafo = prev;
}

// ----------------------------------------------------------------------------

static void get_bounding_box (const aiScene*scene, aiVector3D* min,
                              aiVector3D* max)
{
  aiMatrix4x4 trafo;
  aiIdentityMatrix4(&trafo);

  min->x = min->y = min->z =  1e10f;
  max->x = max->y = max->z = -1e10f;
  get_bounding_box_for_node(scene, scene->mRootNode, min, max, &trafo);
}

// ----------------------------------------------------------------------------

static void color4_to_float4(const aiColor4D *c, float f[4])
{
  f[0] = c->r;
  f[1] = c->g;
  f[2] = c->b;
  f[3] = c->a;
}

// ----------------------------------------------------------------------------

static void set_float4(float f[4], float a, float b, float c, float d)
{
  f[0] = a;
  f[1] = b;
  f[2] = c;
  f[3] = d;
}

// ----------------------------------------------------------------------------
  static void apply_material(gem::plugins::modelloader::material&material
                             , const struct aiMaterial *mtl)
{
  float c[4];

  GLenum fill_mode;
  int ret1, ret2;
  aiColor4D diffuse;
  aiColor4D specular;
  aiColor4D ambient;
  aiColor4D emission;
  float shininess, strength;
  int two_sided;
  int wireframe;
  unsigned int max;

  set_float4(c, 0.8f, 0.8f, 0.8f, 1.0f);
  if(AI_SUCCESS == aiGetMaterialColor(mtl, AI_MATKEY_COLOR_DIFFUSE,
                                      &diffuse)) {
    color4_to_float4(&diffuse, c);
  }
  material.diffuse = {c[0], c[1], c[2], c[3]};

  set_float4(c, 0.0f, 0.0f, 0.0f, 1.0f);
  if(AI_SUCCESS == aiGetMaterialColor(mtl, AI_MATKEY_COLOR_SPECULAR,
                                      &specular)) {
    color4_to_float4(&specular, c);
  }
  material.specular = {c[0], c[1], c[2], c[3]};

  set_float4(c, 0.2f, 0.2f, 0.2f, 1.0f);
  if(AI_SUCCESS == aiGetMaterialColor(mtl, AI_MATKEY_COLOR_AMBIENT,
                                      &ambient)) {
    color4_to_float4(&ambient, c);
  }
  material.ambient = {c[0], c[1], c[2], c[3]};

  set_float4(c, 0.0f, 0.0f, 0.0f, 1.0f);
  if(AI_SUCCESS == aiGetMaterialColor(mtl, AI_MATKEY_COLOR_EMISSIVE,
                                      &emission)) {
    color4_to_float4(&emission, c);
  }
  material.emissive = {c[0], c[1], c[2], c[3]};

  max = 1;
  ret1 = aiGetMaterialFloatArray(mtl, AI_MATKEY_SHININESS, &shininess, &max);
  max = 1;
  ret2 = aiGetMaterialFloatArray(mtl, AI_MATKEY_SHININESS_STRENGTH,
                                 &strength, &max);
  if((ret1 == AI_SUCCESS) && (ret2 == AI_SUCCESS)) {
    material.shininess = shininess * strength;
  } else {
    /* JMZ: in modelOBJ the default shininess is 65 */
    material.shininess = 0.;
    material.specular = {0., 0., 0., 0.};
  }

#if 0
  max = 1;
  if(AI_SUCCESS == aiGetMaterialIntegerArray(mtl, AI_MATKEY_ENABLE_WIREFRAME,
      &wireframe, &max)) {
    fill_mode = wireframe ? GL_LINE : GL_FILL;
  }   else {
    fill_mode = GL_FILL;
  }
  glPolygonMode(GL_FRONT_AND_BACK, fill_mode);

  max = 1;
  if((AI_SUCCESS == aiGetMaterialIntegerArray(mtl, AI_MATKEY_TWOSIDED,
      &two_sided, &max)) && two_sided) {
    glEnable(GL_CULL_FACE);
  } else {
    glDisable(GL_CULL_FACE);
  }
#endif
}


static bool hasMeshes(const struct aiNode* nd) {
  if (nd->mNumMeshes>0)
    return true;
  for (int n = 0; n < nd->mNumChildren; ++n) {
    if (hasMeshes(nd->mChildren[n]))
      return true;
  }
  return false;
}

// ----------------------------------------------------------------------------
static void recursive_render(
  std::vector<struct gem::plugins::modelASSIMP3::meshdata>& meshes
  , const struct aiScene*scene
  , const struct aiScene *sc, const struct aiNode* nd
  , const aiVector2D&tex_scale
  , aiMatrix4x4* trafo
  , unsigned int recursion_depth
  )
{
  int i;
  unsigned int t;
  aiMatrix4x4 prev = *trafo;
  post("%*d: %s(meshes=%d, children=%d)", recursion_depth+1, recursion_depth, __FUNCTION__, nd->mNumMeshes, nd->mNumChildren);
  // update transform
  aiMultiplyMatrix4(trafo,&nd->mTransformation);

  // draw all meshes assigned to this node
  for (unsigned int n=0; n < nd->mNumMeshes; ++n) {
    struct gem::plugins::modelASSIMP3::meshdata newmesh;
    meshes.push_back(std::move(newmesh));
    struct gem::plugins::modelASSIMP3::meshdata&outmesh = meshes.back();
    std::vector<float>&vertices = outmesh.vertices;
    std::vector<float>&normals = outmesh.normals;
    std::vector<float>&texcoords = outmesh.texcoords;
    std::vector<float>&colors = outmesh.colors;
    size_t numVertices = 0;

    const struct aiMesh* mesh = scene->mMeshes[nd->mMeshes[n]];
    post("   %*d: mesh#%d: faces=%d", recursion_depth+1, recursion_depth, n, mesh->mNumFaces);

    apply_material(outmesh.mesh.material, sc->mMaterials[mesh->mMaterialIndex]);

    for (t = 0; t < mesh->mNumFaces; ++t) {
      const struct aiFace* face = &mesh->mFaces[t];

      for(i = 0; i < face->mNumIndices; i++) {
        int index = face->mIndices[i];
        numVertices++;

        if(mesh->mColors[0] != NULL) {
          float *pt = (float*) &mesh->mColors[0][index];
          colors.insert( colors.end(), pt, pt+4);
        }

        if(mesh->mNormals != NULL) {
          float *pt = &mesh->mNormals[index].x;
          normals.insert( normals.end(), pt, pt+3);
        }

        if(mesh->HasTextureCoords(0)) {
          texcoords.push_back(mesh->mTextureCoords[0][index].x * tex_scale.x);
          texcoords.push_back(mesh->mTextureCoords[0][index].y * tex_scale.y);
        }

        aiVector3D tmp = mesh->mVertices[index];
        aiTransformVecByMatrix4(&tmp,trafo);

        float *pt = &tmp.x;
        vertices.insert (vertices.end(), pt, pt+3);
      }
    }
#define SET_OUTMESH(name) outmesh.mesh.name = (name.size()>0) ? name.data() : 0
    SET_OUTMESH(vertices);
    SET_OUTMESH(normals);
    SET_OUTMESH(colors);
    SET_OUTMESH(texcoords);
    outmesh.mesh.size = numVertices;
  }

  // draw all children
  int current_group=0;
  for (unsigned int n = 0; n < nd->mNumChildren; ++n) {
    recursive_render(meshes, scene, sc, nd->mChildren[n], tex_scale,
                     trafo, recursion_depth+1);
  }

  *trafo = prev;
}

};

modelASSIMP3 :: modelASSIMP3(void)
  : m_rebuild(true)
  , m_scene(NULL)
  , m_scale(1.f)
  , m_useMaterial(false)
  , m_refresh(false)
  , m_have_texcoords(false)
  , m_textype("")
  , m_texscale(1., 1.)
  , m_smooth(175.)
  , m_group(0)
{
}

modelASSIMP3 ::~modelASSIMP3(void)
{
  destroy();
}

bool modelASSIMP3 :: open(const std::string&name,
                          const gem::Properties&requestprops)
{
  destroy();
  int flags = aiProcessPreset_TargetRealtime_Quality;
  flags &= ~aiProcess_GenNormals;
  flags &= ~aiProcess_GenSmoothNormals;
  flags |= aiProcess_FlipUVs;

#if 0
  if(m_smooth > 90.)
    flags |= aiProcess_GenSmoothNormals;
  else
    flags |= aiProcess_GenNormals;

  m_scene = aiImportFile(name.c_str(), flags);
#else
  aiPropertyStore *aiprops = aiCreatePropertyStore();
  if(aiprops) {
    flags |= aiProcess_GenSmoothNormals;
    aiSetImportPropertyFloat(aiprops, AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, m_smooth);
  } else {
    if(m_smooth > 90.)
      flags |= aiProcess_GenSmoothNormals;
    else
      flags |= aiProcess_GenNormals;
  }
  m_scene = aiImportFileExWithProperties(name.c_str(), flags, 0, aiprops);
  aiReleasePropertyStore(aiprops);
#endif
  if(!m_scene) {
    return false;
  }

  get_bounding_box(m_scene, &m_min,&m_max);
  m_center.x=(m_min.x+m_max.x)/2.f;
  m_center.y=(m_min.y+m_max.y)/2.f;
  m_center.z=(m_min.z+m_max.z)/2.f;

  /* default is to rescale the object */
  float tmp;
  tmp = m_max.x-m_min.x;
  tmp = aisgl_max(m_max.y - m_min.y, tmp);
  tmp = aisgl_max(m_max.z - m_min.z, tmp);
  m_scale = 2.f / tmp;

  m_offset = m_center * (-m_scale);

  m_rebuild=true;
  m_refresh=true;

  gem::Properties props=requestprops;
  setProperties(props);

  post("%s::%s", __FILE__, __FUNCTION__);
  /* setProperties() already calls render() which compile()s, as m_rebuild=True */
  //compile();
  return true;
}

bool modelASSIMP3 :: render(void)
{
  bool res=true;
  if(m_rebuild) {
    res = compile();
  }
  m_rebuild = false;
  return res;
}
void modelASSIMP3 :: close(void)
{
  destroy();
}

bool modelASSIMP3 :: enumProperties(gem::Properties&readable,
                                    gem::Properties&writeable)
{
  gem::any typ;
  readable.clear();
  readable.set("texwidth", 1);
  readable.set("texheight", 1);

  writeable.clear();
  writeable.set("textype", std::string("UV"));
  writeable.set("_texwidth", 1);
  writeable.set("_texheight", 1);
  writeable.set("rescale", 0);
  writeable.set("smooth", 0);
  writeable.set("usematerials", 0);

  return true;
}

void modelASSIMP3 :: setProperties(gem::Properties&props)
{
  std::vector<std::string>keys=props.keys();
  for(unsigned int i=0; i<keys.size(); i++) {
    std::string key=keys[i];
    std::string s;
    double d;
#if 0
    verbose(1, "[GEM:modelASSIMP3] key[%d]=%s ... %d", i, keys[i].c_str(),
            props.type(keys[i]));
#endif
    if("textype" == key) {
      if(props.get(key, s)) {
        // if there are NO texcoords, we only accept 'linear' and 'spheremap'
        // else, we also allow 'UV'
        // not-accepted textype, simply use the last one
        if(m_have_texcoords && "UV" == s) {
          m_textype = "";
        } else if(("linear" == s) || ("spheremap" == s)) {
          m_textype = s;
        }
        m_rebuild = true;
      }
      continue;
    }
    if("_texwidth" == key) {
      if(props.get(key, d)) {
        if(d != m_texscale.x) {
          m_rebuild=true;
        }

        m_texscale.x = d;
      }
      continue;
    }
    if("_texheight" == key) {
      if(props.get(key, d)) {
        if(d != m_texscale.y) {
          m_rebuild=true;
        }
        m_texscale.y = d;
      }
      continue;
    }

    if("rescale" == key) {
      if(props.get(key, d)) {
        bool b=(bool)d;
        if(b) {
          float tmp;
          tmp = m_max.x-m_min.x;
          tmp = aisgl_max(m_max.y - m_min.y,tmp);
          tmp = aisgl_max(m_max.z - m_min.z,tmp);
          m_scale = 2.f / tmp;
          m_offset = m_center * (-m_scale);
        } else {
          // FIXXME shouldn't this be the default???
          m_scale=1.;
          m_offset.x=m_offset.y=m_offset.z=0.f;
        }
      }
      continue;
    }

    if("usematerials" == key) {
      if(props.get(key, d)) {
        bool useMaterial=d;
        if(useMaterial!=m_useMaterial) {
          m_rebuild=true;
        }
        m_useMaterial=useMaterial;
      }
      continue;
    }

    if("group" == key) {
      if(props.get(key, d)) {
        m_group=d;
        m_rebuild=true;
      }
      continue;
    }

    if("smooth" == key) {
      if(props.get(key, d)) {
        if(d<0.) {
          d=0.;
        }
        m_smooth = d*180.;
        if(m_smooth >= 175.)
          m_smooth = 175.;
        m_rebuild=true;
      }
      continue;
    }

  }

  render();
}
void modelASSIMP3 :: getProperties(gem::Properties&props)
{
  std::vector<std::string>keys=props.keys();
  unsigned int i;
  props.clear();
  for(i=0; i<keys.size(); i++) {
    std::string key=keys[i];
    if("texwidth" == key) {
      props.set(key, m_texscale.x);
      continue;
    }
    if("texheight" == key) {
      props.set(key, m_texscale.y);
      continue;
    }
  }
}

void modelASSIMP3 :: fillVBOarray()
{
  m_VBOarray.clear();
  VBOarray vboarray;

  vboarray.data = &m_vertices;
  vboarray.type = VertexBuffer::GEM_VBO_VERTICES;
  m_VBOarray.push_back(vboarray);

  vboarray.data = &m_normals;
  vboarray.type = VertexBuffer::GEM_VBO_NORMALS;
  m_VBOarray.push_back(vboarray);

  vboarray.data = &m_texcoords;
  vboarray.type = VertexBuffer::GEM_VBO_TEXCOORDS;
  m_VBOarray.push_back(vboarray);

  vboarray.data = &m_colors;
  vboarray.type = VertexBuffer::GEM_VBO_COLORS;
  m_VBOarray.push_back(vboarray);
}

bool modelASSIMP3 :: compile(void)
{
  post("%s::%s", __FILE__, __FUNCTION__);
  if(!m_scene) {
    return false;
  }
  GLboolean useColorMaterial=GL_FALSE;

  // now begin at the root node of the imported data and traverse
  // the scenegraph by multiplying subsequent local transforms
  // together on GL's matrix stack.
  m_meshes.clear();

  aiMatrix4x4 trafo = aiMatrix4x4(aiVector3t<float>(m_scale),
                                  aiQuaterniont<float>(), m_offset);

  recursive_render(m_meshes,
                   m_scene, m_scene, m_scene->mRootNode, m_texscale,
                   &trafo, 0);
  post("%d vertices, %d normals, %d texcoords %d colors"
       , m_vertices.size()
       , m_normals.size()
       , m_texcoords.size()
       , m_colors.size()
    );
  m_have_texcoords = (m_texcoords.size() > 0);

  float texscale[2];
  texscale[0] = m_texscale.x;
  texscale[1] = m_texscale.y;

  if (m_textype.empty() && m_have_texcoords) {;}
  else if("spheremap" == m_textype) {
    modelutils::genTexture_Spheremap(m_texcoords, m_normals, texscale);
  } else {
    modelutils::genTexture_Linear(m_texcoords, m_vertices, texscale);
  }

  fillVBOarray();

  bool res = !(m_vertices.empty() && m_normals.empty()
               && m_texcoords.empty() && m_colors.empty());
  if(res) {
    m_rebuild=false;
    m_refresh=true;
  }
  return res;
}
void modelASSIMP3 :: destroy(void)
{
  if(m_scene) {
    aiReleaseImport(m_scene);
  }
  m_scene=NULL;
}

struct gem::plugins::modelloader::mesh* modelASSIMP3 :: getMesh(size_t meshNum) {
  if (meshNum>=m_meshes.size())
    return nullptr;
  struct meshdata& mesh = m_meshes[meshNum];
  post("got Mesh[%d] @ %p [%d]", meshNum, &(mesh.mesh), mesh.mesh.size);
  return &mesh.mesh;
}
size_t modelASSIMP3 :: getNumMeshes(void) {
  return m_meshes.size();
}

  /**
   * update the mesh data (for all meshes)
   * the data pointers in previously obtained t_mesh'es stay valid
   * (but the data they point to might change)
   * returns TRUE if there was a change, FALSE otherwise
   */
bool modelASSIMP3 :: updateMeshes(void) {
  bool ret = m_refresh || m_rebuild;
  m_refresh = false;
  return ret;
}




std::vector<std::vector<float> > modelASSIMP3 :: getVector(
  std::string vectorName)
{
  if ( vectorName == "vertices" ) {
    return m_vertices;
  }
  if ( vectorName == "normals" ) {
    return m_normals;
  }
  if ( vectorName == "texcoords" ) {
    return m_texcoords;
  }
  if ( vectorName == "colors" ) {
    return m_colors;
  }
  verbose(0, "[GEM:modelASSIMP3] there is no \"%s\" vector !",
          vectorName.c_str());
  return std::vector<std::vector<float> >();
}

std::vector<modelloader::VBOarray> modelASSIMP3 :: getVBOarray()
{
  return m_VBOarray;
}

void modelASSIMP3 :: unsetRefresh()
{
  m_refresh = false;
}
bool modelASSIMP3 :: needRefresh()
{
  return m_refresh;
}
