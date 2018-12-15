#include "CustomFlowScene.h"
#include "CommentGraphicsItem.h"

#include <QAction>
#include <QMouseEvent>
#include <QFileDialog>
#include <QByteArray>
#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QDebug>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QDebug>

#include "../spark-nodes/spark-nodes.h"
#include "../../UrhoDevice.h"

using QtNodes::NodeDataModel;

CustomFlowScene::CustomFlowScene(std::shared_ptr<DataModelRegistry> registry, QObject * parent) :
    FlowScene(registry, parent)
  , _name("New page")
{
    _initialize();
}

CustomFlowScene::CustomFlowScene(QObject * parent) :
    FlowScene(parent)
{
    _initialize();
}

CustomFlowScene::~CustomFlowScene()
{
    delete _commentLayer;
}

void CustomFlowScene::_initialize()
{
    setSceneRect(-100000, -100000, 100000, 100000);

    _mousePos = QPoint(0,0);
    _commentLayer = new QGraphicsItemLayer();

    connect(this, &FlowScene::nodeDoubleClicked, this, &CustomFlowScene::showNode);
    connect(this, &FlowScene::nodeCreated, this, &CustomFlowScene::initNode);
}

void CustomFlowScene::_copyNode()
{
    QList<QGraphicsItem*> selection =  selectedItems();

    QJsonObject json;
    QJsonArray jsonArray;

    Q_FOREACH(QGraphicsItem* item, selection)
    {
        QtNodes::NodeGraphicsObject* graphicsNode = dynamic_cast<QtNodes::NodeGraphicsObject*>(item);
        if (graphicsNode)
        {
            QtNodes::Node& node = graphicsNode->node();
            jsonArray.append(node.save());
        }

        json["SpkgenCopy"] = jsonArray;
    }

    QJsonDocument doc(json);
    QByteArray bytes = doc.toJson();

    QClipboard* clipboard = QApplication::clipboard();
    QMimeData* mimeData = new QMimeData;
    mimeData->setText(bytes);
    clipboard->setMimeData(mimeData);
}

void CustomFlowScene::_pasteNode()
{
    // read clipboard
    const QClipboard* clipboard = QApplication::clipboard();
    const QMimeData* mimeData = clipboard->mimeData();

    // parse clipboard content to json
    QString text = mimeData->text();
    QJsonDocument doc = QJsonDocument::fromJson(text.toLocal8Bit());
    QJsonObject jsonObject = doc.object();

    if (jsonObject.contains("SpkgenCopy"))
    {
        QJsonArray objectList = jsonObject["SpkgenCopy"].toArray();

        // get smallest position for nodes
        double minx = eS32_MAX;
        double miny = eS32_MAX;
        for (int i=0; i<objectList.size(); ++i)
        {
            QJsonObject obj = objectList[i].toObject();
            if (obj.contains("model"))
            {
                QJsonObject pos = obj["position"].toObject();
                QPointF point(pos["x"].toDouble(), pos["y"].toDouble());

                minx = eMin(minx, point.x());
                miny = eMin(miny, point.y());
            }
        }
        QPointF minPos(minx, miny);

        // iterate through all json objects
        for (int i=0; i<objectList.size(); ++i)
        {
            QJsonObject obj = objectList[i].toObject();

            // obj is a node ?
            if (obj.contains("model"))
            {
                QJsonObject model = obj["model"].toObject();
                QString nodeType = model["name"].toString();

                QJsonObject pos = obj["position"].toObject();
                QPointF point(pos["x"].toDouble(), pos["y"].toDouble());

                // get node type from registry
                auto type = registry().create(nodeType);

                if (type)
                {
                    // create node of same type
                    QtNodes::Node& node = createNode(std::move(type));

                    // set new pos using current mouse pos and min pos
                    QPointF newPos = _mousePos - minPos;
                    node.nodeGraphicsObject().setPos(point + newPos);

                    // copy node's parameters
                    BaseNode* baseNode = static_cast<BaseNode*>(node.nodeDataModel());
                    baseNode->restore(model);
                }
            }
        }
    }
}

void CustomFlowScene::_cutNode()
{
    // copy selected nodes
    _copyNode();

    // delete selected nodes
    QList<QGraphicsItem*> selection =  selectedItems();
    Q_FOREACH(QGraphicsItem* item, selection)
    {
        QtNodes::NodeGraphicsObject* graphicsNode = dynamic_cast<QtNodes::NodeGraphicsObject*>(item);
        if (graphicsNode)
        {
            QtNodes::Node& node = graphicsNode->node();
            removeNode(node);
        }
    }
}

void CustomFlowScene::keyPressEvent(QKeyEvent *event)
{
    // if a comment is being editing bypass event
    Q_FOREACH(eCommentItem* ci, _commentList)
    {
        if(ci->isEdited())
        {
            FlowScene::keyPressEvent(event);
            return;
        }
    }

    switch (event->key())
    {
    case Qt::Key_O:
        _addComment(_mousePos);
        break;

    case Qt::Key_C:
        _copyNode();
        break;

    case Qt::Key_V:
        _pasteNode();
        break;

    case Qt::Key_X:
        _cutNode();
        break;
    }

    FlowScene::keyPressEvent(event);
}

void CustomFlowScene::keyReleaseEvent(QKeyEvent *event)
{
    FlowScene::keyReleaseEvent(event);
}

void CustomFlowScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    _mousePos = event->scenePos();
    FlowScene::mouseMoveEvent(event);
}

void CustomFlowScene::showNode(QtNodes::Node& node)
{
    // is a system node ?
    NodeSparkSystem* systemNode = dynamic_cast<NodeSparkSystem*>(node.nodeDataModel());
    if(systemNode)
    {
        if(systemNode->getResult().get() != nullptr)
        {
           UrhoDevice::getInstance()->setCurentParticleSystem(systemNode->getResult());
        }

        return;
    }

    // is a Path node ?
    NodePath* pathNode = dynamic_cast<NodePath*>(node.nodeDataModel());
    if(pathNode)
    {
        if(pathNode->getResult() != nullptr)
        {
            Q_EMIT signalShowPathNode(pathNode);
        }
        return;
    }

    // is a Path4 node ?
    NodePath4* path4Node = dynamic_cast<NodePath4*>(node.nodeDataModel());
    if(path4Node)
    {
        if(path4Node->getResult() != nullptr)
        {
            Q_EMIT signalShowPath4Node(path4Node);
        }
        return;
    }
}

void CustomFlowScene::initNode(QtNodes::Node& node)
{
    BaseNode* baseNode = dynamic_cast<BaseNode*>(node.nodeDataModel());
    baseNode->init();
}

eCommentItem* CustomFlowScene::_addComment(const QPointF &pos)
{
    eCommentItem* comment = new eCommentItem(_commentLayer);
    comment->setPos(_mousePos);
    addItem(comment);

    _commentList.append(comment);

    return comment;
}

void CustomFlowScene::deleteSelectedComments()
{
    for (QGraphicsItem* item : selectedItems())
    {
        if (auto c = qgraphicsitem_cast<eCommentItem*>(item))
        {
            removeItem(c);
            _commentList.removeAll(c);
            delete c;
        }
    }
}

void CustomFlowScene::save(QJsonObject &json) const
{
    // nodes and connections
    QByteArray data = saveToMemory();
    json = QJsonDocument::fromJson(data).object();

    // page name
    json["Name"] = _name;

    // comments
    QJsonArray commentsJsonArray;
    for (auto const & commentItem : _commentList)
    {
        commentsJsonArray.append(commentItem->save());
    }
    json["comments"] = commentsJsonArray;

}

void CustomFlowScene::load(const QJsonObject& json)
{
    clearScene();
    clearComments();

    // page name
    _name = json["Name"].toString();

    // nodes
    QJsonArray nodesJsonArray = json["nodes"].toArray();
    for (QJsonValueRef node : nodesJsonArray)
    {
        restoreNode(node.toObject());
    }

    // connections
    QJsonArray connectionJsonArray = json["connections"].toArray();
    for (QJsonValueRef connection : connectionJsonArray)
    {
        restoreConnection(connection.toObject());
    }

    // comments
    QJsonArray commentsJsonArray = json["comments"].toArray();
    for (int i=0; i<commentsJsonArray.size(); ++i)
    {
        eCommentItem* comment = _addComment(QPointF(0,0));
        comment->restore(commentsJsonArray[i].toObject());
    }
}

void CustomFlowScene::clearComments()
{
    Q_FOREACH(eCommentItem * i, _commentList)
    {
        removeItem(i);
        delete i;
    }
    _commentList.clear();
}
