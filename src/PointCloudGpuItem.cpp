#include "PointCloudGpuItem.h"
#include "PointCloudView.h"
#include "PointCloudGeometry.h"
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPointer>
#include <QQuickWindow>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr int gpuPointLimit = 1000000;
const char *sceneVertex = R"(
attribute vec3 a_position;
attribute float a_intensity;
uniform mat4 u_mvp;
uniform mat4 u_view;
uniform vec2 u_height;
uniform float u_pointPixels;
uniform float u_distance;
uniform float u_dpr;
uniform float u_colorMode;
uniform float u_flat;
uniform vec3 u_flatColor;
varying vec3 v_color;
varying float v_depth;
varying float v_worldZ;
void main() {
    v_worldZ=a_position.z;
    gl_Position = u_mvp * vec4(a_position, 1.0);
    v_depth = max(0.001, -(u_view * vec4(a_position, 1.0)).z);
    float t = u_colorMode > 0.5 ? clamp(a_intensity / 150.0, 0.0, 1.0)
        : clamp((a_position.z-u_height.x)/max(0.1,u_height.y-u_height.x), 0.0, 1.0);
    vec3 low = vec3(0.015,0.14,0.48);
    vec3 mid = vec3(0.025,0.49,0.94);
    vec3 high = vec3(0.025,0.90,0.79);
    vec3 color = t < 0.6 ? mix(low,mid,t/0.6) : mix(mid,high,(t-0.6)/0.4);
    v_color = u_flat > 0.5 ? u_flatColor : color;
    // Fine samples retain holes and edges; the halo is added after depth shading.
    gl_PointSize = clamp(u_pointPixels * sqrt(u_distance / v_depth), 0.9, 3.5)*u_dpr;
})";
const char *sceneFragment = R"(
varying vec3 v_color;
varying float v_depth;
varying float v_worldZ;
uniform float u_clipZ;
uniform float u_flat;
uniform float u_overlay;
uniform float u_far;
uniform float u_fog;
void main() {
    float light = 1.0;
    if (u_flat < 0.5) {
        if(v_worldZ>u_clipZ) discard;
        vec2 d = gl_PointCoord*2.0-1.0;
        if (dot(d,d)>1.0) discard;
        light = 0.85 + 0.15*sqrt(max(0.0,1.0-dot(d,d)));
    }
    float fog = u_overlay>0.5 ? 1.0 : 0.22+0.78*exp(-max(0.0,v_depth-u_fog*0.35)/u_fog);
    float encodedDepth = clamp(log2(1.0+v_depth)/log2(1.0+u_far),0.001,1.0);
    gl_FragColor = vec4(v_color*light*fog,u_overlay>0.5 ? 1.0 : encodedDepth);
})";
const char *quadVertex = R"(
attribute vec2 a_position;
attribute vec2 a_uv;
varying vec2 v_uv;
void main() { v_uv=a_uv; gl_Position=vec4(a_position,0.0,1.0); }
)";
const char *quadFragment = R"(
uniform sampler2D u_scene;
uniform sampler2D u_glow;
uniform vec2 u_pixel;
uniform float u_strength;
uniform float u_glowStrength;
varying vec2 v_uv;
float response(vec2 offset,float depth) {
    float other=texture2D(u_scene,v_uv+offset*u_pixel).a;
    return other>0.0 ? max(0.0,depth-other) : 0.0;
}
void main() {
    vec4 c=texture2D(u_scene,v_uv);
    vec3 background=mix(vec3(0.018,0.029,0.048),vec3(0.025,0.045,0.065),v_uv.y);
    float shade=response(vec2(1.5,0.0),c.a)+response(vec2(-1.5,0.0),c.a)
        +response(vec2(0.0,1.5),c.a)+response(vec2(0.0,-1.5),c.a)
        +response(vec2(1.0,1.0),c.a)+response(vec2(-1.0,-1.0),c.a)
        +response(vec2(1.0,-1.0),c.a)+response(vec2(-1.0,1.0),c.a)
        +0.5*(response(vec2(3.0,0.0),c.a)+response(vec2(-3.0,0.0),c.a)
             +response(vec2(0.0,3.0),c.a)+response(vec2(0.0,-3.0),c.a));
    vec3 color=c.a>0.0 ? c.rgb*exp(-u_strength*shade) : background;
    vec3 halo=texture2D(u_glow,v_uv).rgb*u_glowStrength;
    color += halo*(vec3(1.0)-color);
    gl_FragColor=vec4(color,1.0);
})";
const char *blurFragment = R"(
uniform sampler2D u_image;
uniform vec2 u_step;
varying vec2 v_uv;
void main() {
    vec3 color=texture2D(u_image,v_uv).rgb*0.227027;
    color+=(texture2D(u_image,v_uv+u_step*1.384615).rgb
           +texture2D(u_image,v_uv-u_step*1.384615).rgb)*0.316216;
    color+=(texture2D(u_image,v_uv+u_step*3.230769).rgb
           +texture2D(u_image,v_uv-u_step*3.230769).rgb)*0.070270;
    gl_FragColor=vec4(color,1.0);
})";
}

class CloudGpuRenderer : public QQuickFramebufferObject::Renderer, protected QOpenGLFunctions {
public:
    explicit CloudGpuRenderer(PointCloudView *owner) : m_owner(owner),
        m_points(QOpenGLBuffer::VertexBuffer), m_lines(QOpenGLBuffer::VertexBuffer),
        m_quad(QOpenGLBuffer::VertexBuffer) {}
    void synchronize(QQuickFramebufferObject *item) override {
        auto *cloud=static_cast<PointCloudGpuItem *>(item);
        m_state=cloud->owner()->state();
        m_window=item->window();
    }
    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        return new QOpenGLFramebufferObject(size,format);
    }
    void render() override;
private:
    bool initialize();
    void uploadPoints(const QVector3D &target);
    void drawLines(const QVector<MapPoint> &vertices, GLenum mode, const QVector3D &color, bool overlay);
    void fail(const QString &message) {
        qWarning().noquote()<<"[PointCloudGpu]"<<message;
        const auto owner=m_owner;
        if(owner) QMetaObject::invokeMethod(owner,[owner]{if(owner)owner->disableGpu();},Qt::QueuedConnection);
        m_failed=true;
    }
    QPointer<PointCloudView> m_owner;
    QPointer<QQuickWindow> m_window;
    PointCloudView::State m_state;
    QOpenGLShaderProgram m_sceneProgram,m_quadProgram,m_blurProgram;
    QOpenGLBuffer m_points,m_lines,m_quad;
    QOpenGLVertexArrayObject m_vao;
    std::unique_ptr<QOpenGLFramebufferObject> m_scene;
    std::unique_ptr<QOpenGLFramebufferObject> m_glow[2];
    bool m_initialized=false,m_failed=false;
    // Pin the uploaded point storage so an allocator cannot reuse its address.
    // Pose-only snapshots may share it without forcing another large upload.
    std::shared_ptr<const MappingSnapshot> m_uploadedSnapshot;
    const MapPoint *m_uploadedData=nullptr;
    int m_uploadedSourceCount=0;
    QVector3D m_uploadedTarget;
    qreal m_uploadedDistance=0;
    int m_pointCount=0;
    float m_minZ=0,m_maxZ=3;
    QMatrix4x4 m_view,m_mvp;
};

bool CloudGpuRenderer::initialize() {
    initializeOpenGLFunctions();
    const bool es=QOpenGLContext::currentContext()->isOpenGLES();
    const QByteArray prefix=es ? QByteArray("precision highp float;\n") : QByteArray("#version 120\n");
    if(!m_sceneProgram.addShaderFromSourceCode(QOpenGLShader::Vertex,prefix+sceneVertex)
       ||!m_sceneProgram.addShaderFromSourceCode(QOpenGLShader::Fragment,prefix+sceneFragment)) {
        fail(m_sceneProgram.log()); return false;
    }
    m_sceneProgram.bindAttributeLocation("a_position",0);
    m_sceneProgram.bindAttributeLocation("a_intensity",1);
    if(!m_sceneProgram.link()) {fail(m_sceneProgram.log());return false;}
    if(!m_quadProgram.addShaderFromSourceCode(QOpenGLShader::Vertex,prefix+quadVertex)
       ||!m_quadProgram.addShaderFromSourceCode(QOpenGLShader::Fragment,prefix+quadFragment)) {
        fail(m_quadProgram.log());return false;
    }
    m_quadProgram.bindAttributeLocation("a_position",0);
    m_quadProgram.bindAttributeLocation("a_uv",1);
    if(!m_quadProgram.link()) {fail(m_quadProgram.log());return false;}
    if(!m_blurProgram.addShaderFromSourceCode(QOpenGLShader::Vertex,prefix+quadVertex)
       ||!m_blurProgram.addShaderFromSourceCode(QOpenGLShader::Fragment,prefix+blurFragment)) {
        fail(m_blurProgram.log());return false;
    }
    m_blurProgram.bindAttributeLocation("a_position",0);
    m_blurProgram.bindAttributeLocation("a_uv",1);
    if(!m_blurProgram.link()) {fail(m_blurProgram.log());return false;}
    m_vao.create();
    if(m_vao.isCreated())m_vao.bind();
    m_points.create();m_points.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_lines.create();m_lines.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_quad.create();m_quad.bind();
    const float quad[]={-1,-1,0,0, 1,-1,1,0, -1,1,0,1, 1,1,1,1};
    m_quad.allocate(quad,sizeof(quad));m_quad.release();
    if(m_vao.isCreated())m_vao.release();
    qInfo()<<"[PointCloudGpu] OpenGL renderer:"<<reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    return true;
}

void CloudGpuRenderer::uploadPoints(const QVector3D &target) {
    const auto &snapshot=m_state.snapshot;
    if(!snapshot) {m_pointCount=0;m_uploadedSnapshot.reset();m_uploadedData=nullptr;return;}
    if(m_uploadedSnapshot && m_uploadedData==snapshot->points.constData()
       && m_uploadedSourceCount==snapshot->points.size()
       && (target-m_uploadedTarget).lengthSquared()<1.0f
       && std::abs(m_state.distance-m_uploadedDistance)<qMax(.1,m_state.distance*.1))return;
    QVector<MapPoint> selected;
    selected.reserve(qMin(gpuPointLimit,snapshot->points.size()));
    const auto bounds=cloudBounds(snapshot->points);
    m_minZ=bounds.valid?bounds.low.z():0;
    m_maxZ=bounds.valid?bounds.high.z():3;
    const float nearRadius=qMax(5.0,m_state.distance*0.8);
    const float nearSquared=nearRadius*nearRadius;
    int nearCount=0;
    for(const auto &point:snapshot->points) {
        const float dx=point.x-target.x(),dy=point.y-target.y(),dz=point.z-target.z();
        if(dx*dx+dy*dy+dz*dz<=nearSquared)++nearCount;
    }
    if(snapshot->points.size()<=gpuPointLimit) {
        // At normal room sizes the complete dense map fits without any thinning.
        selected=snapshot->points;
    } else {
        const int farCount=snapshot->points.size()-nearCount;
        const int nearBudget=qMin(nearCount,gpuPointLimit-qMin(farCount,250000));
        const int farBudget=qMin(farCount,gpuPointLimit-nearBudget);
        quint64 nearAccumulator=0,farAccumulator=0;
        // Fractional sampling uses the full budget. Integer strides otherwise
        // halve visible density abruptly when the map crosses a threshold.
        for(const auto &point:snapshot->points) {
            const float dx=point.x-target.x(),dy=point.y-target.y(),dz=point.z-target.z();
            const bool nearby=dx*dx+dy*dy+dz*dz<=nearSquared;
            auto &accumulator=nearby?nearAccumulator:farAccumulator;
            const int count=nearby?nearCount:farCount;
            accumulator+=nearby?nearBudget:farBudget;
            if(count>0 && accumulator>=quint64(count)) {
                selected.append(point);accumulator-=count;
            }
        }
    }
    if(selected.isEmpty()) {m_minZ=0;m_maxZ=3;}
    m_points.bind();
    m_points.allocate(selected.constData(),selected.size()*int(sizeof(MapPoint)));
    m_points.release();
    m_pointCount=selected.size();m_uploadedSnapshot=snapshot;m_uploadedTarget=target;
    m_uploadedData=snapshot->points.constData();m_uploadedSourceCount=snapshot->points.size();
    m_uploadedDistance=m_state.distance;
}

void CloudGpuRenderer::drawLines(const QVector<MapPoint>&vertices,GLenum mode,const QVector3D&color,bool overlay) {
    if(vertices.isEmpty())return;
    m_sceneProgram.bind();
    m_sceneProgram.setUniformValue("u_flat",1.0f);
    m_sceneProgram.setUniformValue("u_flatColor",color);
    m_sceneProgram.setUniformValue("u_overlay",overlay?1.0f:0.0f);
    m_lines.bind();m_lines.allocate(vertices.constData(),vertices.size()*int(sizeof(MapPoint)));
    m_sceneProgram.enableAttributeArray(0);
    m_sceneProgram.setAttributeBuffer(0,GL_FLOAT,0,3,sizeof(MapPoint));
    m_sceneProgram.disableAttributeArray(1);
    glDrawArrays(mode,0,vertices.size());
    m_lines.release();m_sceneProgram.release();
}

void CloudGpuRenderer::render() {
    if(m_failed)return;
    if(!m_initialized) {m_initialized=initialize();if(!m_initialized)return;}
    if(m_vao.isCreated())m_vao.bind();
    const QSize size=framebufferObject()->size();
    if(!m_scene || m_scene->size()!=size) {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setInternalTextureFormat(0x881A); // RGBA16F retains depth-shading precision.
        m_scene.reset(new QOpenGLFramebufferObject(size,format));
        if(!m_scene->isValid()) {
            format.setInternalTextureFormat(GL_RGBA);
            m_scene.reset(new QOpenGLFramebufferObject(size,format));
        }
        if(!m_scene->isValid()) {fail(QStringLiteral("Cannot allocate point-cloud framebuffer"));return;}
    }
    QVector3D target=m_state.target;
    const auto snapshot=m_state.snapshot;
    uploadPoints(target);
    const double pi=3.14159265358979323846;
    const float yaw=float(m_state.yaw*pi/180.0);
    const float pitch=float((m_state.topDown?89.5:m_state.pitch)*pi/180.0);
    const QVector3D eye=target+QVector3D(std::cos(pitch)*std::cos(yaw),
                                      std::cos(pitch)*std::sin(yaw),std::sin(pitch))*float(m_state.distance);
    m_view.setToIdentity();m_view.lookAt(eye,target,QVector3D(0,0,1));
    QMatrix4x4 projection;
    const float farPlane=float(qMax(1000.0,m_state.distance*10));
    projection.perspective(50.0f,float(size.width())/qMax(1,size.height()),
                           float(qMax(0.02,m_state.distance*0.001)),farPlane);
    m_mvp=projection*m_view;
    m_scene->bind();glViewport(0,0,size.width(),size.height());
    glDisable(GL_BLEND);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);
    if(!QOpenGLContext::currentContext()->isOpenGLES()) {
        glEnable(0x8642); // PROGRAM_POINT_SIZE
        if (QOpenGLContext::currentContext()->format().profile()!=QSurfaceFormat::CoreProfile)
            glEnable(0x8861); // POINT_SPRITE supplies gl_PointCoord in compatibility contexts.
    }
    glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    m_sceneProgram.bind();
    m_sceneProgram.setUniformValue("u_mvp",m_mvp);
    m_sceneProgram.setUniformValue("u_view",m_view);
    const float clipZ=m_state.cutaway?m_minZ+(m_maxZ-m_minZ)*.65f:1.0e7f;
    m_sceneProgram.setUniformValue("u_height",QVector2D(m_minZ,m_state.cutaway?clipZ:m_maxZ));
    m_sceneProgram.setUniformValue("u_clipZ",clipZ);
    m_sceneProgram.setUniformValue("u_pointPixels",float(m_state.pointSize));
    m_sceneProgram.setUniformValue("u_distance",float(m_state.distance));
    m_sceneProgram.setUniformValue("u_dpr",m_window?float(m_window->devicePixelRatio()):1.0f);
    m_sceneProgram.setUniformValue("u_colorMode",float(m_state.colorMode));
    m_sceneProgram.setUniformValue("u_far",farPlane);
    m_sceneProgram.setUniformValue("u_fog",float(m_state.distance*.95+3));
    m_sceneProgram.release();
    const float step=std::pow(10.0f,std::floor(std::log10(qMax(0.1,m_state.distance/8.0))));
    const int half=qBound(10,int(m_state.distance*2/step),80);
    const float cx=std::floor(target.x()/step)*step,cy=std::floor(target.y()/step)*step;
    const float floor=m_pointCount?m_minZ-0.03f:0.0f;
    QVector<MapPoint> grid;
    grid.reserve((half*2+1)*4);
    for(int i=-half;i<=half;++i) {
        grid.append({cx+i*step,cy-half*step,floor,0});grid.append({cx+i*step,cy+half*step,floor,0});
        grid.append({cx-half*step,cy+i*step,floor,0});grid.append({cx+half*step,cy+i*step,floor,0});
    }
    // Keep the reference grid out of the point-cloud depth and EDL buffers.
    if (!m_pointCount) drawLines(grid,GL_LINES,QVector3D(.055f,.09f,.12f),false);
    if(m_pointCount) {
        m_sceneProgram.bind();m_sceneProgram.setUniformValue("u_flat",0.0f);
        m_sceneProgram.setUniformValue("u_overlay",0.0f);
        m_points.bind();
        m_sceneProgram.enableAttributeArray(0);m_sceneProgram.enableAttributeArray(1);
        m_sceneProgram.setAttributeBuffer(0,GL_FLOAT,0,3,sizeof(MapPoint));
        m_sceneProgram.setAttributeBuffer(1,GL_FLOAT,12,1,sizeof(MapPoint));
        glDrawArrays(GL_POINTS,0,m_pointCount);
        m_points.release();m_sceneProgram.release();
    }
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_BLEND);
    if (m_state.glow) {
        // Blur only the visible RGB. Encoded depth in scene alpha stays untouched.
        const QSize glowSize(qMax(1,size.width()/2),qMax(1,size.height()/2));
        for (auto &buffer:m_glow) if (!buffer || buffer->size()!=glowSize) {
            buffer.reset(new QOpenGLFramebufferObject(glowSize));
            if (!buffer->isValid()) { fail(QStringLiteral("Cannot allocate glow framebuffer")); return; }
        }
        m_blurProgram.bind();m_quad.bind();
        m_blurProgram.enableAttributeArray(0);m_blurProgram.enableAttributeArray(1);
        m_blurProgram.setAttributeBuffer(0,GL_FLOAT,0,2,4*sizeof(float));
        m_blurProgram.setAttributeBuffer(1,GL_FLOAT,2*sizeof(float),2,4*sizeof(float));
        m_blurProgram.setUniformValue("u_image",0);
        glActiveTexture(GL_TEXTURE0);
        for (int pass=0;pass<2;++pass) {
            m_glow[pass]->bind();glViewport(0,0,glowSize.width(),glowSize.height());
            glBindTexture(GL_TEXTURE_2D,pass==0?m_scene->texture():m_glow[0]->texture());
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            m_blurProgram.setUniformValue("u_step",pass==0?QVector2D(1.4f/size.width(),0):QVector2D(0,1.4f/size.height()));
            glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        }
        m_quad.release();m_blurProgram.release();
    }
    framebufferObject()->bind();glViewport(0,0,size.width(),size.height());
    glClearColor(.027f,.067f,.10f,1);glClear(GL_COLOR_BUFFER_BIT);
    m_quadProgram.bind();m_quad.bind();
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,m_scene->texture());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    m_quadProgram.setUniformValue("u_scene",0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,m_state.glow?m_glow[1]->texture():m_scene->texture());
    m_quadProgram.setUniformValue("u_glow",1);
    m_quadProgram.setUniformValue("u_glowStrength",m_state.glow?.55f:0.0f);
    m_quadProgram.setUniformValue("u_pixel",QVector2D(1.0f/size.width(),1.0f/size.height()));
    m_quadProgram.setUniformValue("u_strength",m_state.depthShading?28.0f:0.0f);
    m_quadProgram.enableAttributeArray(0);m_quadProgram.enableAttributeArray(1);
    m_quadProgram.setAttributeBuffer(0,GL_FLOAT,0,2,4*sizeof(float));
    m_quadProgram.setAttributeBuffer(1,GL_FLOAT,2*sizeof(float),2,4*sizeof(float));
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    m_quad.release();m_quadProgram.release();
    if(snapshot) {
        QVector<MapPoint> path;
        const int stride=qMax(1,(snapshot->trajectory.size()+9999)/10000);
        path.reserve(qMin(10000,snapshot->trajectory.size()));
        for(int i=0;i<snapshot->trajectory.size();i+=stride) {
            const auto p=snapshot->trajectory[i];path.append({p.x(),p.y(),p.z(),0});
        }
        if (!snapshot->trajectory.isEmpty() && (snapshot->trajectory.size()-1)%stride!=0) {
            const auto p=snapshot->trajectory.last();path.append({p.x(),p.y(),p.z(),0});
        }
        glLineWidth(1.5f*(m_window?m_window->devicePixelRatio():1));
        drawLines(path,GL_LINE_STRIP,QVector3D(.83f,.93f,.98f),true);
        if(snapshot->hasPose) {
            QVector3D forward=snapshot->orientation.rotatedVector(QVector3D(1,0,0));
            forward.setZ(0);if(forward.lengthSquared()<.001f)forward=QVector3D(1,0,0);forward.normalize();
            const QVector3D right(-forward.y(),forward.x(),0);
            const float scale=float(m_state.distance*.018);
            const QVector3D p=snapshot->position;
            const QVector3D a=p+forward*scale,b=p-forward*scale*.6f+right*scale*.45f,
                            c=p-forward*scale*.6f-right*scale*.45f;
            drawLines({{a.x(),a.y(),a.z(),0},{b.x(),b.y(),b.z(),0},{c.x(),c.y(),c.z(),0}},
                      GL_TRIANGLES,QVector3D(1.0f,.22f,.14f),true);
        }
    }
    glDepthMask(GL_TRUE);glBindTexture(GL_TEXTURE_2D,0);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,0);
    if(m_vao.isCreated())m_vao.release();
    if(m_window)m_window->resetOpenGLState();
}

PointCloudGpuItem::PointCloudGpuItem(PointCloudView *owner)
    : QQuickFramebufferObject(owner),m_owner(owner) {
    setMirrorVertically(true);
}
QQuickFramebufferObject::Renderer *PointCloudGpuItem::createRenderer() const {
    return new CloudGpuRenderer(m_owner);
}
