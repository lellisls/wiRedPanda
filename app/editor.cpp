#include "box.h"
#include "boxnotfoundexception.h"
#include "buzzer.h"
#include "commands.h"
#include "editor.h"
#include "globalproperties.h"
#include "graphicelement.h"
#include "input.h"
#include "mainwindow.h"
#include "nodes/qneconnection.h"
#include "serializationfunctions.h"
#include "thememanager.h"

#include <iostream>
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDrag>
#include <QFileDialog>
#include <QGraphicsItem>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMimeData>
#include <QSettings>
#include <QtMath>

Editor*Editor::globalEditor = nullptr;

Editor::Editor( QObject *parent ) : QObject( parent ), scene( nullptr ) {
  if( !globalEditor ) {
    globalEditor = this;
  }
  mainWindow = qobject_cast< MainWindow* >( parent );
  markingSelectionBox = false;
  _editedConn_id = 0;
  undoStack = new QUndoStack( this );
  scene = new Scene( this );

  boxManager = new BoxManager( mainWindow, this );

  install( scene );
  draggingElement = false;
  clear( );
  timer.start( );
  mShowWires = true;
  mShowGates = true;
  connect( this, &Editor::circuitHasChanged, simulationController, &SimulationController::reSortElms );
}

Editor::~Editor( ) {
}

void Editor::updateTheme( ) {
  if( ThemeManager::globalMngr ) {
    const ThemeAttrs attrs = ThemeManager::globalMngr->getAttrs( );
    scene->setBackgroundBrush( attrs.scene_bgBrush );
    scene->setDots( QPen( attrs.scene_bgDots ) );
    selectionRect->setBrush( QBrush( attrs.selectionBrush ) );
    selectionRect->setPen( QPen( attrs.selectionPen, 1, Qt::SolidLine ) );
    for( GraphicElement *elm : scene->getElements( ) ) {
      elm->updateTheme( );
    }
    for( QNEConnection *conn : scene->getConnections( ) ) {
      conn->updateTheme( );
    }
  }
}

void Editor::mute( bool _mute ) {
  for( GraphicElement *elm : scene->getElements( ) ) {
    Buzzer *bz = dynamic_cast< Buzzer* >( elm );
    if( bz ) {
      bz->mute( _mute );
    }
  }
}

void Editor::install( Scene *s ) {
  s->installEventFilter( this );
  simulationController = new SimulationController( s );
  simulationController->start( );
  clear( );
}

QNEConnection* Editor::getEditedConn( ) {
  return( dynamic_cast< QNEConnection* >( ElementFactory::getItemById( _editedConn_id ) ) );
}

void Editor::setEditedConn( QNEConnection *editedConn ) {
  if( editedConn ) {
    editedConn->setFocus( );
    _editedConn_id = editedConn->id( );
  }
  else {
    _editedConn_id = 0;
  }
}


void Editor::buildSelectionRect( ) {
  selectionRect = new QGraphicsRectItem( );
  selectionRect->setFlag( QGraphicsItem::ItemIsSelectable, false );
  if( scene ) {
    scene->addItem( selectionRect );
  }
}

void Editor::clear( ) {
  simulationController->stop( );
  simulationController->clear( );
  boxManager->clear( );
  ElementFactory::instance->clear( );
  undoStack->clear( );
  if( scene ) {
    scene->clear( );
  }
  buildSelectionRect( );
  if( !scene->views( ).isEmpty( ) ) {
    scene->setSceneRect( scene->views( ).front( )->rect( ) );
  }
  updateTheme( );
  simulationController->start( );
  emit circuitHasChanged( );
}

void Editor::deleteAction( ) {
  const QList< QGraphicsItem* > &items = scene->selectedItems( );
  scene->clearSelection( );
  if( !items.isEmpty( ) ) {
    receiveCommand( new DeleteItemsCommand( items, this ) );
  }
}

void Editor::showWires( bool checked ) {
  mShowWires = checked;
  for( QGraphicsItem *item : scene->items( ) ) {
    GraphicElement *elm = qgraphicsitem_cast< GraphicElement* >( item );
    if( ( item->type( ) == QNEConnection::Type ) ) {
      item->setVisible( checked );
    }
    else if( ( item->type( ) == GraphicElement::Type ) && elm ) {
      if( elm->elementType( ) == ElementType::NODE ) {
        elm->setVisible( checked );
      }
      else {
        for( QNEPort *in : elm->inputs( ) ) {
          in->setVisible( checked );
        }
        for( QNEPort *out : elm->outputs( ) ) {
          out->setVisible( checked );
        }
      }
    }
  }
}

void Editor::showGates( bool checked ) {
  mShowGates = checked;
  for( QGraphicsItem *item : scene->items( ) ) {
    GraphicElement *elm = qgraphicsitem_cast< GraphicElement* >( item );
    if( ( item->type( ) == GraphicElement::Type ) && elm ) {
      if( ( elm->elementGroup( ) != ElementGroup::INPUT ) &&
          ( elm->elementGroup( ) != ElementGroup::OUTPUT ) ) {
        item->setVisible( checked );
      }
    }
  }
}

void Editor::rotate( bool rotateRight ) {
  double angle = 90.0;
  if( !rotateRight ) {
    angle = -angle;
  }
  QList< QGraphicsItem* > list = scene->selectedItems( );
  QList< GraphicElement* > elms;
  for( QGraphicsItem *item : list ) {
    GraphicElement *elm = qgraphicsitem_cast< GraphicElement* >( item );
    if( elm && ( elm->type( ) == GraphicElement::Type ) ) {
      elms.append( elm );
    }
  }
  if( ( elms.size( ) > 1 ) || ( ( elms.size( ) == 1 ) && elms.front( )->rotatable( ) ) ) {
    receiveCommand( new RotateCommand( elms, angle ) );
  }
}

void Editor::flipH( ) {
  QList< QGraphicsItem* > list = scene->selectedItems( );
  QList< GraphicElement* > elms;
  for( QGraphicsItem *item : list ) {
    GraphicElement *elm = qgraphicsitem_cast< GraphicElement* >( item );
    if( elm && ( elm->type( ) == GraphicElement::Type ) ) {
      elms.append( elm );
    }
  }
  if( ( elms.size( ) > 1 ) || ( ( elms.size( ) == 1 ) ) ) {
    receiveCommand( new FlipCommand( elms, 0 ) );
  }
}


void Editor::flipV( ) {
  QList< QGraphicsItem* > list = scene->selectedItems( );
  QList< GraphicElement* > elms;
  for( QGraphicsItem *item : list ) {
    GraphicElement *elm = qgraphicsitem_cast< GraphicElement* >( item );
    if( elm && ( elm->type( ) == GraphicElement::Type ) ) {
      elms.append( elm );
    }
  }
  if( ( elms.size( ) > 1 ) || ( ( elms.size( ) == 1 ) ) ) {
    receiveCommand( new FlipCommand( elms, 1 ) );
  }
}

QList< QGraphicsItem* > Editor::itemsAt( QPointF pos ) {
  QRectF rect( pos - QPointF( 4, 4 ), QSize( 9, 9 ) );
  return( scene->items( rect.normalized( ) ) );
}


QGraphicsItem* Editor::itemAt( QPointF pos ) {
  QList< QGraphicsItem* > items = scene->items( pos );
  items.append( itemsAt( pos ) );
  for( QGraphicsItem *item : items ) {
    if( item->type( ) == QNEPort::Type ) {
      return( item );
    }
  }
  for( QGraphicsItem *item : items ) {
    if( item->type( ) > QGraphicsItem::UserType ) {
      return( item );
    }
  }
  return( nullptr );
}

ElementEditor* Editor::getElementEditor( ) const {
  return( _elementEditor );
}

QPointF Editor::getMousePos( ) const {
  return( mousePos );
}

SimulationController* Editor::getSimulationController( ) const {
  return( simulationController );
}

void Editor::addItem( QGraphicsItem *item ) {
  scene->addItem( item );
}

void Editor::deleteEditedConn( ) {
  QNEConnection *editedConn = getEditedConn( );
  if( editedConn ) {
    scene->removeItem( editedConn );
    delete editedConn;
  }
  setEditedConn( nullptr );
}

void Editor::startNewConnection( QNEOutputPort *startPort ) {
  QNEConnection *editedConn = ElementFactory::buildConnection( );
  editedConn->setStart( startPort );
  editedConn->setEndPos( mousePos );
  addItem( editedConn );
  setEditedConn( editedConn );
  editedConn->updatePath( );
}


void Editor::startNewConnection( QNEInputPort *endPort ) {
  QNEConnection *editedConn = ElementFactory::buildConnection( );
  editedConn->setEnd( endPort );
  editedConn->setStartPos( mousePos );
  addItem( editedConn );
  setEditedConn( editedConn );
  editedConn->updatePath( );
}


void Editor::detachConnection( QNEInputPort *endPort ) {
  QNEConnection *editedConn = endPort->connections( ).last( );
  QNEOutputPort *startPort = editedConn->start( );
  if( startPort ) {
    receiveCommand( new DeleteItemsCommand( editedConn, this ) );
    startNewConnection( startPort );
  }
}

void Editor::startSelectionRect( ) {
  selectionStartPoint = mousePos;
  markingSelectionBox = true;
  selectionRect->setRect( QRectF( selectionStartPoint, selectionStartPoint ) );
  selectionRect->show( );
  selectionRect->update( );
}

bool Editor::mousePressEvt( QGraphicsSceneMouseEvent *mouseEvt ) {
  QGraphicsItem *item = itemAt( mousePos );
  if( item && ( item->type( ) == QNEPort::Type ) ) {
    /* When the mouse pressed over an connected input port, the line
     * is disconnected and can be connected in an other port. */
    QNEPort *pressedPort = qgraphicsitem_cast< QNEPort* >( item );
    QNEConnection *editedConn = getEditedConn( );
    if( editedConn ) {
      makeConnection( editedConn );
    }
    else {
      if( pressedPort->isOutput( ) ) {
        QNEOutputPort *startPort = dynamic_cast< QNEOutputPort* >( pressedPort );
        startNewConnection( startPort );
      }
      else {
        QNEInputPort *endPort = dynamic_cast< QNEInputPort* >( pressedPort );
        if( endPort->connections( ).size( ) > 0 ) {
          detachConnection( endPort );
        }
        else {
          startNewConnection( endPort );
        }
      }
    }
    return( true );
  }
  else {
    if( getEditedConn( ) ) {
      deleteEditedConn( );
    }
    else if( !item && ( mouseEvt->button( ) == Qt::LeftButton ) ) {
      /* Mouse pressed over board (Selection box). */
      startSelectionRect( );
    }
  }
  return( false );
}

void Editor::resizeScene( ) {
  QVector< GraphicElement* > elms = scene->getElements( );
  if( !elms.isEmpty( ) ) {
    QRectF rect = scene->sceneRect( );
    for( GraphicElement *elm : elms ) {
      QRectF itemRect = elm->boundingRect( ).translated( elm->pos( ) );
      rect = rect.united( itemRect.adjusted( -10, -10, 10, 10 ) );
    }
    scene->setSceneRect( rect );
  }
  QGraphicsItem *item = itemAt( mousePos );
  if( item && ( timer.elapsed( ) > 100 ) && draggingElement ) {
    if( !scene->views( ).isEmpty( ) ) {
      QGraphicsView *view = scene->views( ).front( );
      view->ensureVisible( QRectF( mousePos - QPointF( 4, 4 ), QSize( 9, 9 ) ).normalized( ) );
    }
    timer.restart( );
  }
}

bool Editor::mouseMoveEvt( QGraphicsSceneMouseEvent *mouseEvt ) {
  Q_UNUSED( mouseEvt )
  QNEConnection * editedConn = getEditedConn( );
  if( editedConn ) {
    /* If a connection is being created, the ending coordinate follows the mouse position. */
    if( editedConn->start( ) ) {
      editedConn->setEndPos( mousePos );
      editedConn->updatePath( );
    }
    else if( editedConn->end( ) ) {
      editedConn->setStartPos( mousePos );
      editedConn->updatePath( );
    }
    else {
      deleteEditedConn( );
    }
    return( true );
  }
  else if( markingSelectionBox ) {
    /* If is marking the selectionBox, the last coordinate follows the mouse position. */
    QRectF rect = QRectF( selectionStartPoint, mousePos ).normalized( );
    selectionRect->setRect( rect );
    QPainterPath selectionBox;
    selectionBox.addRect( rect );
    scene->setSelectionArea( selectionBox );
  }
  else if( !markingSelectionBox ) {
    /* Else, the selectionRect is hidden. */
    selectionRect->hide( );
  }
  return( false );
}

void Editor::makeConnection( QNEConnection *editedConn ) {
  QNEPort *port = dynamic_cast< QNEPort* >( itemAt( mousePos ) );
  if( port && editedConn ) {
    /* The mouse is released over a QNEPort. */
    QNEOutputPort *startPort = nullptr;
    QNEInputPort *endPort = nullptr;
    if( editedConn->start( ) != nullptr ) {
      startPort = editedConn->start( );
      endPort = dynamic_cast< QNEInputPort* >( port );
    }
    else if( editedConn->end( ) != nullptr ) {
      startPort = dynamic_cast< QNEOutputPort* >( port );
      endPort = editedConn->end( );
    }
    if( !startPort || !endPort ) {
      return;
    }
    /* Verifying if the connection is valid. */
    if( ( startPort->graphicElement( ) != endPort->graphicElement( ) ) && !startPort->isConnected( endPort ) ) {
      /* Making connection. */
      editedConn->setStart( startPort );
      editedConn->setEnd( endPort );
      receiveCommand( new AddItemsCommand( editedConn, this ) );
      setEditedConn( nullptr );
    }
    else {
      deleteEditedConn( );
    }
  }
}

bool Editor::mouseReleaseEvt( QGraphicsSceneMouseEvent *mouseEvt ) {
  if( !mouseEvt ) {
    return( false );
  }
  /* When mouse is released the selection rect is hidden. */
  selectionRect->hide( );
  markingSelectionBox = false;
  if( QApplication::overrideCursor( ) ) {
    QApplication::setOverrideCursor( Qt::ArrowCursor );
  }
  QNEConnection *editedConn = getEditedConn( );
  if( editedConn && !( mouseEvt->buttons( ) & Qt::LeftButton ) ) {
    /* A connection is being created, and left button was released. */
    makeConnection( editedConn );
    return( true );
  }
  return( false );
}

void Editor::handleHoverPort( ) {
  QNEPort *port = dynamic_cast< QNEPort* >( itemAt( mousePos ) );
  QNEPort *hoverPort = getHoverPort( );
  if( hoverPort && ( port != hoverPort ) ) {
    releaseHoverPort( );
  }
  if( port && ( port->type( ) == QNEPort::Type ) ) {
    QNEConnection *editedConn = getEditedConn( );
    releaseHoverPort( );
    setHoverPort( port );
    if( editedConn && editedConn->start( ) && ( editedConn->start( )->isOutput( ) == port->isOutput( ) ) ) {
      QApplication::setOverrideCursor( QCursor( Qt::ForbiddenCursor ) );
    }
  }
}

void Editor::releaseHoverPort( ) {
  QNEPort *hoverPort = getHoverPort( );
  if( hoverPort ) {
    hoverPort->hoverLeave( );
    setHoverPort( nullptr );
    QApplication::setOverrideCursor( QCursor( ) );
  }
}

void Editor::setHoverPort( QNEPort *port ) {
  if( port ) {
    GraphicElement *hoverElm = port->graphicElement( );
    port->hoverEnter( );
    if( hoverElm && ElementFactory::contains( hoverElm->id( ) ) ) {
      _hoverPortElm_id = hoverElm->id( );
      for( int i = 0; i < ( hoverElm->inputSize( ) + hoverElm->outputSize( ) ); ++i ) {
        if( i < hoverElm->inputSize( ) ) {
          if( port == hoverElm->input( i ) ) {
            _hoverPort_nbr = i;
          }
        }
        else if( port == hoverElm->output( i - hoverElm->inputSize( ) ) ) {
          _hoverPort_nbr = i;
        }
      }
    }
  }
  else {
    _hoverPortElm_id = 0;
    _hoverPort_nbr = 0;
  }
}

QNEPort* Editor::getHoverPort( ) {
  GraphicElement *hoverElm = dynamic_cast< GraphicElement* >( ElementFactory::getItemById( _hoverPortElm_id ) );
  QNEPort *hoverPort = nullptr;
  if( hoverElm ) {
    if( _hoverPort_nbr < hoverElm->inputSize( ) ) {
      hoverPort = hoverElm->input( _hoverPort_nbr );
    }
    else if( ( ( _hoverPort_nbr - hoverElm->inputSize( ) ) < hoverElm->outputSize( ) ) ) {
      hoverPort = hoverElm->output( _hoverPort_nbr - hoverElm->inputSize( ) );
    }
  }
  if( !hoverPort ) {
    setHoverPort( nullptr );
  }
  return( hoverPort );
}

bool Editor::dropEvt( QGraphicsSceneDragDropEvent *dde ) {
  /* Verify if mimetype is compatible. */
  if( dde->mimeData( )->hasFormat( "application/x-dnditemdata" ) ) {
    /* Extracting mimedata from drop event. */
    QByteArray itemData = dde->mimeData( )->data( "application/x-dnditemdata" );
    QDataStream dataStream( &itemData, QIODevice::ReadOnly );
    QPointF offset;
    QString label_auxData;
    qint32 type;
    dataStream >> offset >> type >> label_auxData;
    QPointF pos = dde->scenePos( ) - offset;
    dde->accept( );

    GraphicElement *elm = ElementFactory::buildElement( static_cast< ElementType >( type ) );
    /* If element type is unknown, a default element is created with the pixmap received from mimedata */
    if( !elm ) {
      return( false );
    }
    if( elm->elementType( ) == ElementType::BOX ) {
      try {
        Box *box = dynamic_cast< Box* >( elm );
        if( box ) {
          QString fname = label_auxData;
          if( !boxManager->loadBox( box, fname, GlobalProperties::currentFile ) ) {
            return( false );
          }
        }
      }
      catch( std::runtime_error &err ) {
        QMessageBox::warning( mainWindow, tr( "Error" ), QString::fromStdString( err.what( ) ) );
        return( false );
      }
    }
    int wdtOffset = ( 64 - elm->boundingRect( ).width( ) ) / 2;
    if( wdtOffset > 0 ) {
      pos = pos + QPointF( wdtOffset, wdtOffset );
    }
    /*
     * TODO: Rotate all element icons, remake the port position logic, and remove the code below.
     * Rotating element in 90 degrees.
     */
    if( elm->rotatable( ) && ( elm->elementType( ) != ElementType::NODE ) ) {
      elm->setRotation( 90 );
    }
    /* Adding the element to the scene. */
    receiveCommand( new AddItemsCommand( elm, this ) );
    /* Cleaning the selection. */
    scene->clearSelection( );
    /* Setting created element as selected. */
    elm->setSelected( true );
    /* Adjusting the position of the element. */
    elm->setPos( pos );

    return( true );
  }
  else if( dde->mimeData( )->hasFormat( "application/ctrlDragData" ) ) {

    QByteArray itemData = dde->mimeData( )->data( "application/ctrlDragData" );
    QDataStream ds( &itemData, QIODevice::ReadOnly );

    QPointF offset;
    ds >> offset;
    offset = dde->scenePos( ) - offset;
    dde->accept( );


    QPointF ctr;
    ds >> ctr;
    double version = GlobalProperties::version;
    QList< QGraphicsItem* > itemList = SerializationFunctions::deserialize( ds,
                                                                            version,
                                                                            GlobalProperties::currentFile );
    receiveCommand( new AddItemsCommand( itemList, this ) );
    scene->clearSelection( );
    for( QGraphicsItem *item : itemList ) {
      if( item->type( ) == GraphicElement::Type ) {
        item->setPos( ( item->pos( ) + offset ) );
        item->setSelected( true );
        item->update( );
      }
    }
    resizeScene( );

    return( true );
  }
  return( false );
}

bool Editor::dragMoveEvt( QGraphicsSceneDragDropEvent *dde ) {
  /* Accepting drag/drop event of the following mimedata format. */
  if( dde->mimeData( )->hasFormat( "application/x-dnditemdata" ) ) {
    return( true );
  }
  if( dde->mimeData( )->hasFormat( "application/ctrlDragData" ) ) {
    return( true );
  }
  return( false );
}

bool Editor::wheelEvt( QWheelEvent *wEvt ) {
  if( wEvt ) {
    int numDegrees = wEvt->delta( ) / 8;
    int numSteps = numDegrees / 15;
    if( wEvt->orientation( ) == Qt::Horizontal ) {
      emit scroll( numSteps, 0 );
    }
    else {
      emit scroll( 0, numSteps );
    }
    wEvt->accept( );
    return( true );
  }
  return( false );
}

void Editor::ctrlDrag( QPointF pos ) {
  COMMENT( "Ctrl + Drag action triggered.", 0 );
  QVector< GraphicElement* > selectedElms = scene->selectedElements( );
  if( !selectedElms.isEmpty( ) ) {
    QRectF rect;
    for( GraphicElement *elm : selectedElms ) {
      rect = rect.united( elm->boundingRect( ).translated( elm->pos( ) ) );
    }
    rect = rect.adjusted( -8, -8, 8, 8 );
    QImage image( rect.size( ).toSize( ), QImage::Format_ARGB32 );
    image.fill( Qt::transparent );

    QPainter painter( &image );
    painter.setOpacity( 0.25 );
    scene->render( &painter, image.rect( ), rect );

    QByteArray itemData;
    QDataStream dataStream( &itemData, QIODevice::WriteOnly );

    QPointF offset = pos - rect.topLeft( );
    dataStream << pos;

    copy( scene->selectedItems( ), dataStream );

    QMimeData *mimeData = new QMimeData;
    mimeData->setData( "application/ctrlDragData", itemData );

    QDrag *drag = new QDrag( this );
    drag->setMimeData( mimeData );
    drag->setPixmap( QPixmap::fromImage( image ) );
    drag->setHotSpot( offset.toPoint( ) );
    drag->exec( Qt::CopyAction, Qt::CopyAction );
  }
}

QUndoStack* Editor::getUndoStack( ) const {
  return( undoStack );
}

Scene* Editor::getScene( ) const {
  return( scene );
}

void Editor::cut( const QList< QGraphicsItem* > &items, QDataStream &ds ) {
  copy( items, ds );
  deleteAction( );
}

void Editor::copy( const QList< QGraphicsItem* > &items, QDataStream &ds ) {
  QPointF center( static_cast< qreal >( 0.0f ), static_cast< qreal >( 0.0f ) );
  float elm = 0;
  for( QGraphicsItem *item : items ) {
    if( item->type( ) == GraphicElement::Type ) {
      center += item->pos( );
      elm++;
    }
  }
  ds << center / static_cast< qreal >( elm );
  SerializationFunctions::serialize( scene->selectedItems( ), ds );
}

void Editor::paste( QDataStream &ds ) {
  scene->clearSelection( );
  QPointF ctr;
  ds >> ctr;
  QPointF offset = mousePos - ctr - QPointF( static_cast< qreal >( 32.0f ), static_cast< qreal >( 32.0f ) );
  double version = GlobalProperties::version;
  QList< QGraphicsItem* > itemList = SerializationFunctions::deserialize( ds,
                                                                          version,
                                                                          GlobalProperties::currentFile );
  receiveCommand( new AddItemsCommand( itemList, this ) );
  for( QGraphicsItem *item : itemList ) {
    if( item->type( ) == GraphicElement::Type ) {
      item->setPos( ( item->pos( ) + offset ) );
      item->update( );
      item->setSelected( true );
    }
  }
  resizeScene( );
}

void Editor::selectAll( ) {
  for( QGraphicsItem *item : scene->items( ) ) {
    item->setSelected( true );
  }
}

void Editor::save( QDataStream &ds ) {
  ds << QApplication::applicationName( ) + " " + QString::number( GlobalProperties::version );
  ds << scene->sceneRect( );
  SerializationFunctions::serialize( scene->items( ), ds );
}

void Editor::load( QDataStream &ds ) {
  clear( );
  simulationController->stop( );
  SerializationFunctions::load( ds, GlobalProperties::currentFile, scene );
  simulationController->start( );
  scene->clearSelection( );
  emit circuitHasChanged( );
}

void Editor::setElementEditor( ElementEditor *value ) {
  _elementEditor = value;
  _elementEditor->setScene( scene );
  _elementEditor->setEditor( this );
  connect( _elementEditor, &ElementEditor::sendCommand, this, &Editor::receiveCommand );
}

QPointF roundTo( QPointF point, int multiple ) {
  int x = static_cast< int >( point.x( ) );
  int y = static_cast< int >( point.y( ) );
  int nx = multiple * qFloor( x / multiple );
  int ny = multiple * qFloor( y / multiple );
  return( QPointF( nx, ny ) );
}

void Editor::contextMenu( QPoint screenPos ) {
  QGraphicsItem *item = itemAt( mousePos );
  if( item ) {
    if( scene->selectedItems( ).contains( item ) ) {
      _elementEditor->contextMenu( screenPos );
    }
    else if( ( item->type( ) == GraphicElement::Type ) ) {
      scene->clearSelection( );
      item->setSelected( true );
      _elementEditor->contextMenu( screenPos );
    }
  }
  else {
    QMenu menu;
    QAction *pasteAction = menu.addAction( QIcon( QPixmap( ":/toolbar/paste.png" ) ), tr( "Paste" ) );
    const QClipboard *clipboard = QApplication::clipboard( );
    const QMimeData *mimeData = clipboard->mimeData( );
    if( mimeData->hasFormat( "wpanda/copydata" ) ) {
      connect( pasteAction, &QAction::triggered, this, &Editor::pasteAction );
    }
    else {
      pasteAction->setEnabled( false );
    }
    menu.exec( screenPos );
  }
}

void Editor::updateVisibility( ) {
  showGates( mShowGates );
  showWires( mShowWires );
}

void Editor::receiveCommand( QUndoCommand *cmd ) {
  undoStack->push( cmd );
}

void Editor::copyAction( ) {
  QVector< GraphicElement* > elms = scene->selectedElements( );
  if( elms.empty( ) ) {
    QClipboard *clipboard = QApplication::clipboard( );
    clipboard->clear( );
  }
  else {
    QClipboard *clipboard = QApplication::clipboard( );
    QMimeData *mimeData = new QMimeData;
    QByteArray itemData;
    QDataStream dataStream( &itemData, QIODevice::WriteOnly );
    copy( scene->selectedItems( ), dataStream );
    mimeData->setData( "wpanda/copydata", itemData );
    clipboard->setMimeData( mimeData );
  }
}

void Editor::cutAction( ) {
  QClipboard *clipboard = QApplication::clipboard( );
  QMimeData *mimeData = new QMimeData( );
  QByteArray itemData;
  QDataStream dataStream( &itemData, QIODevice::WriteOnly );
  cut( scene->selectedItems( ), dataStream );
  mimeData->setData( "wpanda/copydata", itemData );
  clipboard->setMimeData( mimeData );
}

void Editor::pasteAction( ) {
  const QClipboard *clipboard = QApplication::clipboard( );
  const QMimeData *mimeData = clipboard->mimeData( );
  if( mimeData->hasFormat( "wpanda/copydata" ) ) {
    QByteArray itemData = mimeData->data( "wpanda/copydata" );
    QDataStream dataStream( &itemData, QIODevice::ReadOnly );
    paste( dataStream );
  }
}

bool Editor::eventFilter( QObject *obj, QEvent *evt ) {
  if( obj == scene ) {
    QGraphicsSceneDragDropEvent *dde = dynamic_cast< QGraphicsSceneDragDropEvent* >( evt );
    QGraphicsSceneMouseEvent *mouseEvt = dynamic_cast< QGraphicsSceneMouseEvent* >( evt );
    QWheelEvent *wEvt = dynamic_cast< QWheelEvent* >( evt );
    QKeyEvent *keyEvt = dynamic_cast< QKeyEvent* >( evt );
    if( mouseEvt ) {
      mousePos = mouseEvt->scenePos( );
      resizeScene( );
      handleHoverPort( );
      if( mouseEvt->modifiers( ) & Qt::ShiftModifier ) {
        mouseEvt->setModifiers( Qt::ControlModifier );
        return( QObject::eventFilter( obj, evt ) );
      }
    }
    bool ret = false;
    if( mouseEvt && ( ( evt->type( ) == QEvent::GraphicsSceneMousePress )
                      || ( evt->type( ) == QEvent::GraphicsSceneMouseDoubleClick ) ) ) {
      QGraphicsItem *item = itemAt( mousePos );
      if( item && ( mouseEvt->button( ) == Qt::LeftButton ) ) {
        if( ( mouseEvt->modifiers( ) & Qt::ControlModifier ) && ( item->type( ) == GraphicElement::Type ) ) {
          item->setSelected( true );
          ctrlDrag( mouseEvt->scenePos( ) );
          return( true );
        }
        draggingElement = true;
        /* STARTING MOVING ELEMENT */
/*        qDebug() << "IN"; */
        QList< QGraphicsItem* > list = scene->selectedItems( );
        list.append( itemsAt( mousePos ) );
        movedElements.clear( );
        oldPositions.clear( );
        for( QGraphicsItem *it : list ) {
          GraphicElement *elm = qgraphicsitem_cast< GraphicElement* >( it );
          if( elm ) {
            movedElements.append( elm );
            oldPositions.append( elm->pos( ) );
          }
        }
      }
      else if( mouseEvt->button( ) == Qt::RightButton ) {
        contextMenu( mouseEvt->screenPos( ) );
      }
    }
    if( evt->type( ) == QEvent::GraphicsSceneMouseRelease ) {
      if( draggingElement && ( mouseEvt->button( ) == Qt::LeftButton ) ) {
        if( !movedElements.empty( ) ) {
/*
 *          if( movedElements.size( ) != oldPositions.size( ) ) {
 *            throw std::runtime_error( ERRORMSG(tr( "Invalid coordinates." ).toStdString( ) ));
 *          }
 *          qDebug() << "OUT";
 */
          bool valid = false;
          for( int elm = 0; elm < movedElements.size( ); ++elm ) {
            if( movedElements[ elm ]->pos( ) != oldPositions[ elm ] ) {
              valid = true;
              break;
            }
          }
          if( valid ) {
            receiveCommand( new MoveCommand( movedElements, oldPositions ) );
          }
        }
        draggingElement = false;
        movedElements.clear( );
      }
    }
    switch( static_cast< int >( evt->type( ) ) ) {
        case QEvent::GraphicsSceneMousePress: {
        ret = mousePressEvt( mouseEvt );
        break;
      }
        case QEvent::GraphicsSceneMouseMove: {
        ret = mouseMoveEvt( mouseEvt );
        break;
      }
        case QEvent::GraphicsSceneMouseRelease: {
        ret = mouseReleaseEvt( mouseEvt );
        break;
      }
        case QEvent::GraphicsSceneDrop: {
        ret = dropEvt( dde );
        break;
      }
        case QEvent::GraphicsSceneDragMove:
        case QEvent::GraphicsSceneDragEnter: {
        ret = dragMoveEvt( dde );
        break;
      }
        case QEvent::GraphicsSceneWheel: {
        ret = wheelEvt( wEvt );
        break;
      }
        case QEvent::GraphicsSceneMouseDoubleClick: {
        QNEConnection *connection = dynamic_cast< QNEConnection* >( itemAt( mousePos ) );
        if( connection && ( connection->type( ) == QNEConnection::Type ) ) {
          /* Mouse pressed over a connection. */
          if( connection ) {
            if( connection->start( ) && connection->end( ) ) {
              receiveCommand( new SplitCommand( connection, mousePos, this ) );
            }
          }
          evt->accept( );
          return( true );
        }
        break;
      }
        case QEvent::KeyPress: {
        if( keyEvt && !( keyEvt->modifiers( ) & Qt::ControlModifier ) ) {
          for( GraphicElement *elm : scene->getElements( ) ) {
            if( elm->hasTrigger( ) && !elm->getTrigger( ).isEmpty( ) ) {
              Input *in = dynamic_cast< Input* >( elm );
              if( in && elm->getTrigger( ).matches( keyEvt->key( ) ) ) {
                if( elm->elementType( ) == ElementType::SWITCH ) {
                  in->setOn( !in->getOn( ) );
                }
                else {
                  in->setOn( true );
                }
              }
            }
          }
        }
        break;
      }
        case QEvent::KeyRelease: {
        if( keyEvt && !( keyEvt->modifiers( ) & Qt::ControlModifier ) ) {
          for( GraphicElement *elm : scene->getElements( ) ) {
            if( elm->hasTrigger( ) && !elm->getTrigger( ).isEmpty( ) ) {
              Input *in = dynamic_cast< Input* >( elm );
              if( in && ( elm->getTrigger( ).matches( keyEvt->key( ) ) == QKeySequence::ExactMatch ) ) {
                if( elm->elementType( ) != ElementType::SWITCH ) {
                  in->setOn( false );
                }
              }
            }
          }
        }
        break;
      }
    }
    if( ret ) {
      return( ret );
    }
  }
  return( QObject::eventFilter( obj, evt ) );
}
