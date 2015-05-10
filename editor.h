#ifndef EDITOR_H
#define EDITOR_H


#include <QGraphicsScene>
#include <QObject>

#include "simulationcontroller.h"
#include <nodes/qneconnection.h>
#include <elementfactory.h>
#include <elementeditor.h>

class Editor : public QObject {
  Q_OBJECT
public:
  explicit Editor(QObject *parent = 0);
  ~Editor();
  void install(QGraphicsScene * s);
  void save(QDataStream &ds);
  void load(QDataStream &ds);
  void cut(QDataStream & ds);
  void copy(QDataStream & ds);
  void paste(QDataStream & ds);
  void selectAll();
signals:
  void scrollHorizontally( int numSteps );
  void scrollVertically( int numSteps );

public slots:
  void clear();
  void showWires(bool checked);
  void rotate(bool rotateRight);
private:
  QGraphicsScene * scene;
  QGraphicsItem * itemAt( const QPointF & pos );
  QNEConnection * conn;
  ElementFactory factory;
  ElementEditor * elementEditor;
  bool markingSelectionBox;
  QGraphicsRectItem * selectionRect;
  QPointF selectionStartPoint;
  SimulationController * simulationController;
  QPointF mousePos;
  // QObject interface
public:
  bool eventFilter(QObject *obj, QEvent *evt);
  void deleteElements();
  void setElementEditor(ElementEditor * value);
};

#endif // EDITOR_H
