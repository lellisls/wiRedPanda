#include "simplewaveform.h"
#include "ui_simplewaveform.h"

#include "input.h"

#include <cmath>
#include <QBuffer>
#include <QChartView>
#include <QClipboard>
#include <QDebug>
#include <QLineSeries>
#include <QMessageBox>
#include <QMimeData>
#include <QPointF>
#include <QSettings>
/* #include <QSvgGenerator> */
#include <bitset>
#include <QValueAxis>

using namespace QtCharts;


class SCStop {
  SimulationController *sc;
  bool restart = false;
public:
  SCStop( SimulationController *sc ) : sc( sc ) {
    if( sc->isRunning( ) ) {
      restart = true;
      sc->stop( );
    }
  }

  void release( ) {
    if( restart ) {
      sc->start( );
    }
  }
  ~SCStop( ) {
    release( );
  }
};

SimpleWaveform::SimpleWaveform( Editor *editor, QWidget *parent ) : QDialog( parent ), ui( new Ui::SimpleWaveform ),
  editor( editor ) {
  ui->setupUi( this );
  resize( 800, 500 );
  chart.legend( )->setAlignment( Qt::AlignLeft );

/*  chart.legend( )->hide( ); */
/*  chart->addSeries(series); */

/*  chart.setTitle( "Simple Waveform View." ); */

  chartView = new QChartView( &chart, this );
  chartView->setRenderHint( QPainter::Antialiasing );
  ui->gridLayout->addWidget( chartView );
  setWindowTitle( "Simple WaveForm - WaveDolphin Beta" );
  setWindowFlags( Qt::Window );
  setModal( true );
  sortingKind = SortingKind::POSITION;
  QSettings settings( QSettings::IniFormat, QSettings::UserScope,
                      QApplication::organizationName( ), QApplication::applicationName( ) );
  settings.beginGroup( "SimpleWaveform" );
  restoreGeometry( settings.value( "geometry" ).toByteArray( ) );
  settings.endGroup( );
}

SimpleWaveform::~SimpleWaveform( ) {
  QSettings settings( QSettings::IniFormat, QSettings::UserScope,
                      QApplication::organizationName( ), QApplication::applicationName( ) );
  settings.beginGroup( "SimpleWaveform" );
  settings.setValue( "geometry", saveGeometry( ) );
  settings.endGroup( );
  delete ui;
}

void SimpleWaveform::sortElements( QVector< GraphicElement* > &elements, QVector< GraphicElement* > &inputs,
                                   QVector< GraphicElement* > &outputs, SortingKind sorting ) {
  elements = ElementMapping::sortGraphicElements( elements );
  for( GraphicElement *elm : elements ) {
    if( elm && ( elm->type( ) == GraphicElement::Type ) ) {
      if( elm->elementGroup( ) == ElementGroup::INPUT ) {
        inputs.append( elm );
      }
      else if( elm->elementGroup( ) == ElementGroup::OUTPUT ) {
        outputs.append( elm );
      }
    }
  }
  std::stable_sort( inputs.begin( ), inputs.end( ), [ ]( GraphicElement *elm1, GraphicElement *elm2 ) {
    return( elm1->pos( ).ry( ) < elm2->pos( ).ry( ) );
  } );
  std::stable_sort( outputs.begin( ), outputs.end( ), [ ]( GraphicElement *elm1, GraphicElement *elm2 ) {
    return( elm1->pos( ).ry( ) < elm2->pos( ).ry( ) );
  } );

  std::stable_sort( inputs.begin( ), inputs.end( ), [ ]( GraphicElement *elm1, GraphicElement *elm2 ) {
    return( elm1->pos( ).rx( ) < elm2->pos( ).rx( ) );
  } );
  std::stable_sort( outputs.begin( ), outputs.end( ), [ ]( GraphicElement *elm1, GraphicElement *elm2 ) {
    return( elm1->pos( ).rx( ) < elm2->pos( ).rx( ) );
  } );
  if( sorting == SortingKind::INCREASING ) {
    std::stable_sort( inputs.begin( ), inputs.end( ),
                      [ ]( GraphicElement *elm1, GraphicElement*
                           elm2 ) {
      return( QString::compare( elm1->getLabel( ).toUtf8( ), elm2->getLabel( ).toUtf8( ),
                                Qt::CaseInsensitive ) <= 0 );
    } );
    std::stable_sort( outputs.begin( ), outputs.end( ),
                      [ ]( GraphicElement *elm1, GraphicElement*
                           elm2 ) {
      return( QString::compare( elm1->getLabel( ).toUtf8( ), elm2->getLabel( ).toUtf8( ),
                                Qt::CaseInsensitive ) <= 0 );
    } );
  }
  else if( sorting == SortingKind::DECREASING ) {
    std::stable_sort( inputs.begin( ), inputs.end( ),
                      [ ]( GraphicElement *elm1, GraphicElement*
                           elm2 ) {
      return( QString::compare( elm1->getLabel( ).toUtf8( ), elm2->getLabel( ).toUtf8( ),
                                Qt::CaseInsensitive ) >= 0 );
    } );
    std::stable_sort( outputs.begin( ), outputs.end( ),
                      [ ]( GraphicElement *elm1, GraphicElement*
                           elm2 ) {
      return( QString::compare( elm1->getLabel( ).toUtf8( ), elm2->getLabel( ).toUtf8( ),
                                Qt::CaseInsensitive ) >= 0 );
    } );
  }
}

bool SimpleWaveform::saveToTxt( QTextStream &outStream, Editor *editor ) {
  QVector< GraphicElement* > elements = editor->getScene( )->getElements( );
  QVector< GraphicElement* > inputs;
  QVector< GraphicElement* > outputs;

  // Sorting elements according to the radion option. All elements initially in elements vector. Then, inputs and
  // outputs are extracted and sorted from it.
  sortElements( elements, inputs, outputs, SortingKind::POSITION );
  if( elements.isEmpty( ) || inputs.isEmpty( ) || outputs.isEmpty( ) ) {
    return( false );
  }
  // Getting digital circuit simulator.
  SimulationController *sc = editor->getSimulationController( );
  // Creating class to pause main window simulator while creating waveform.
  SCStop scst( sc );

  // Getting initial value from inputs and writing them to oldvalues. Used to save current state of inputs and restore
  // it after simulation.
  QVector< char > oldValues( inputs.size( ) );
  for( int in = 0; in < inputs.size( ); ++in ) {
    oldValues[ in ] = inputs[ in ]->output( )->value( );
  }
  // Computing number of iterations based on the number of inputs.
  int num_iter = pow( 2, inputs.size( ) );
  // Getting the number of outputs. Warning: will not work if inout element type in created.
  int outputCount = 0;
  for( GraphicElement *out : outputs ) {
    outputCount += out->inputSize( );
  }
  // Creating vector results with the output resulting values.
  QVector< QVector< uchar > > results( outputCount, QVector< uchar >( num_iter ) );
  for( int itr = 0; itr < num_iter; ++itr ) {
    // For each iteration, set a distinct value for the inputs. The value is the bit values corresponding to the number
    // of current iteration.
    std::bitset< std::numeric_limits< unsigned int >::digits > bs( itr );
    for( int in = 0; in < inputs.size( ); ++in ) {
      uchar val = bs[ in ];
      dynamic_cast< Input* >( inputs[ in ] )->setOn( val );
    }
    // Updating the values of the circuit logic based on current input values.
    sc->update( );
    sc->updateAll( );
    // Setting the computed output values to the waveform results vector.
    int counter = 0;
    for( int out = 0; out < outputs.size( ); ++out ) {
      int inSz = outputs[ out ]->inputSize( );
      for( int port = inSz - 1; port >= 0; --port ) {
        uchar val = outputs[ out ]->input( port )->value( );
        results[ counter ][ itr ] = val;
        counter++;
      }
    }
  }
  // Writing the input values at each iteration to the output stream.
  for( int in = 0; in < inputs.size( ); ++in ) {
    QString label = inputs[ in ]->getLabel( );
    if( label.isEmpty( ) ) {
      label = ElementFactory::translatedName( inputs[ in ]->elementType( ) );
    }
    for( int itr = 0; itr < num_iter; ++itr ) {
      std::bitset< std::numeric_limits< unsigned int >::digits > bs( itr );
      outStream << static_cast< int >( bs[ in ] );
    }
    outStream << " : \"" << label << "\"\n";
  }
  outStream << "\n";
  // Writing the output values at each iteration to the output stream.
  int counter = 0;
  for( int out = 0; out < outputs.size( ); ++out ) {
    QString label = outputs[ out ]->getLabel( );
    if( label.isEmpty( ) ) {
      label = ElementFactory::translatedName( outputs[ out ]->elementType( ) );
    }
    int inSz = outputs[ out ]->inputSize( );
    for( int port = inSz - 1; port >= 0; --port ) {
      for( int itr = 0; itr < num_iter; ++itr ) {
        outStream << static_cast< int >( results[ counter ][ itr ] );
      }
      counter += 1;
      outStream << " : \"" << label << "[" << port << "]\"\n";
    }
  }
  // Restoring the old values to the inputs, prior to simulaton.
  for( int in = 0; in < inputs.size( ); ++in ) {
    dynamic_cast< Input* >( inputs[ in ] )->setOn( oldValues[ in ] );
  }
  // Resuming digital circuit main window after waveform simulation is finished.
  scst.release( );
  return( true );
}

void SimpleWaveform::showWaveform( ) {
  QSettings settings( QSettings::IniFormat, QSettings::UserScope,
                      QApplication::organizationName( ), QApplication::applicationName( ) );
  settings.beginGroup( "waveform" );
  // Getting sorting type.
  if( settings.contains( "sortingType" ) ) {
    sortingKind = static_cast< SortingKind >( settings.value( "sortingType" ).toInt( ) );
  }
  settings.endGroup( );
  switch( sortingKind ) {
      case SortingKind::DECREASING:
      ui->radioButton_Decreasing->setChecked( true );
      break;
      case SortingKind::INCREASING:
      ui->radioButton_Increasing->setChecked( true );
      break;
      case SortingKind::POSITION:
      ui->radioButton_Position->setChecked( true );
      break;
  }
  int gap = 2;

  chart.removeAllSeries( );

  // Getting digital circuit simulator.
  SimulationController *sc = editor->getSimulationController( );
  // Creating class to pause main window simulator while creating waveform.
  SCStop scst( sc );

  QVector< GraphicElement* > elements = editor->getScene( )->getElements( );
  QVector< GraphicElement* > inputs;
  QVector< GraphicElement* > outputs;

  // Sorting elements according to the radion option. All elements initially in elements vector. Then, inputs and
  // outputs are extracted and sorted from it.
  sortElements( elements, inputs, outputs, sortingKind );
  if( elements.isEmpty( ) ) {
    QMessageBox::warning( parentWidget( ), tr( "Error" ), tr( "Could not find any port for the simulation" ) );
    return;
  }
  if( inputs.isEmpty( ) ) {
    QMessageBox::warning( parentWidget( ), tr( "Error" ), tr( "Could not find any input for the simulation." ) );
    return;
  }
  if( outputs.isEmpty( ) ) {
    QMessageBox::warning( parentWidget( ), tr( "Error" ), tr( "Could not find any output for the simulation." ) );
    return;
  }
  if( inputs.size( ) > 8 ) {
    QMessageBox::warning( parentWidget( ), tr( "Error" ), tr( "The simulation is limited to 8 inputs." ) );
    return;
  }
  QVector< QLineSeries* > in_series;
  QVector< QLineSeries* > out_series;

  // Getting initial value from inputs and writing them to oldvalues. Used to save current state of inputs and restore
  // it after simulation.
  // Also getting the name of the inputs. If no label is given, the element type is used as a name. Bug here? What if
  // there are 2 inputs without name or two identical labels?
  QVector< char > oldValues( inputs.size( ) );
  for( int in = 0; in < inputs.size( ); ++in ) {
    in_series.append( new QLineSeries( this ) );
    QString label = inputs[ in ]->getLabel( );
    if( label.isEmpty( ) ) {
      label = ElementFactory::translatedName( inputs[ in ]->elementType( ) );
    }
    in_series[ in ]->setName( label );
    oldValues[ in ] = inputs[ in ]->output( )->value( );
  }
  // Getting the name of the outputs. If no label is given, the element type is used as a name. Bug here? What if there
  // are 2 outputs without name or two identical labels?
  for( int out = 0; out < outputs.size( ); ++out ) {
    QString label = outputs[ out ]->getLabel( );
    if( label.isEmpty( ) ) {
      label = ElementFactory::translatedName( outputs[ out ]->elementType( ) );
    }
    for( int port = 0; port < outputs[ out ]->inputSize( ); ++port ) {
      out_series.append( new QLineSeries( this ) );
      if( outputs[ out ]->inputSize( ) > 1 ) {
        out_series.last( )->setName( QString( "%1_%2" ).arg( label ).arg( port ) );
      }
      else {
        out_series.last( )->setName( label );
      }
    }
  }
  //qDebug( ) << in_series.size( ) << " inputs";
  //qDebug( ) << out_series.size( ) << " outputs";

  // Computing number of iterations based on the number of inputs.
  int num_iter = pow( 2, in_series.size( ) );
  //qDebug( ) << "Num iter = " << num_iter;
/*  gap += outputs.size( ) % 2; */
  // Running simulation.
  for( int itr = 0; itr < num_iter; ++itr ) {
    // For each iteration, set a distinct value for the inputs. The value is the bit values corresponding to the number
    // of current iteration.
    std::bitset< std::numeric_limits< unsigned int >::digits > bs( itr );
    //qDebug( ) << itr;
    for( int in = 0; in < inputs.size( ); ++in ) {
      float val = bs[ in ];
      dynamic_cast< Input* >( inputs[ in ] )->setOn( not qFuzzyIsNull( val ) );
      float offset = ( in_series.size( ) - in - 1 + out_series.size( ) ) * 2 + gap + 0.5;
      in_series[ in ]->append( itr, static_cast< qreal >( offset + val ) );
      in_series[ in ]->append( itr + 1, static_cast< qreal >( offset + val ) );
    }
    // Updating the values of the circuit logic based on current input values.
    sc->update( );
    sc->updateAll( );
    // Setting the computed output values to the waveform results.
    int counter = 0;
    for( int out = 0; out < outputs.size( ); ++out ) {
      int inSz = outputs[ out ]->inputSize( );
      for( int port = inSz - 1; port >= 0; --port ) {
        float val = outputs[ out ]->input( port )->value( ) > 0;
        float offset = ( out_series.size( ) - counter - 1 ) * 2 + 0.5;
        out_series[ counter ]->append( itr, static_cast< qreal >( offset + val ) );
        out_series[ counter ]->append( itr + 1, static_cast< qreal >( offset + val ) );
/*        cout << counter << " " << out; */
        counter++;
      }
    }
  }
  // Inserting input series to the chart
  for( QLineSeries *in : in_series ) {
    chart.addSeries( in );
  }
  // Inserting output series to the chart
  for( QLineSeries *out : out_series ) {
    chart.addSeries( out );
  }
  // Setting graphic axes
  chart.createDefaultAxes( );

/*  chart.axisY( )->hide( ); */
  // Setting range and names to x, y axis.
  QValueAxis *ax = dynamic_cast< QValueAxis* >( chart.axisX( ) );
  ax->setRange( 0, num_iter );
  ax->setTickCount( num_iter + 1 );
  ax->setLabelFormat( QString( "%i" ) );
  QValueAxis *ay = dynamic_cast< QValueAxis* >( chart.axisY( ) );
/*  ay->setShadesBrush( QBrush( Qt::lightGray ) ); */

  // Setting graphics waveform color.
  ay->setShadesColor( QColor( 0, 0, 0, 8 ) );
  ay->setShadesPen( QPen( QColor( 0, 0, 0, 0 ) ) );
  ay->setShadesVisible( true );

  ay->setGridLineVisible( false );
  ay->setTickCount( ( in_series.size( ) + out_series.size( ) + gap / 2 + 1 ) );
  ay->setRange( 0, in_series.size( ) * 2 + out_series.size( ) * 2 + gap );
  ay->setGridLineColor( Qt::transparent );
  ay->setLabelsVisible( false );
/*  ay->hide( ); */

  // Executing QDialog. Opens window to the user.
  exec( );
  // Restoring the old values to the inputs, prior to simulaton.
  for( int in = 0; in < inputs.size( ); ++in ) {
    dynamic_cast< Input* >( inputs[ in ] )->setOn( oldValues[ in ] );

  }
  // Resuming digital circuit main window after waveform simulation is finished.
  scst.release( );
}

void SimpleWaveform::on_radioButton_Position_clicked( ) {
  QSettings settings( QSettings::IniFormat, QSettings::UserScope,
                      QApplication::organizationName( ), QApplication::applicationName( ) );
  settings.beginGroup( "waveform" );
  sortingKind = SortingKind::POSITION;
  settings.setValue( "sortingType", static_cast< int >( sortingKind ) );
  settings.endGroup( );

  showWaveform( );
}

void SimpleWaveform::on_radioButton_Increasing_clicked( ) {
  QSettings settings( QSettings::IniFormat, QSettings::UserScope,
                      QApplication::organizationName( ), QApplication::applicationName( ) );
  settings.beginGroup( "waveform" );
  sortingKind = SortingKind::INCREASING;
  settings.setValue( "sortingType", static_cast< int >( sortingKind ) );
  settings.endGroup( );

  showWaveform( );
}

void SimpleWaveform::on_radioButton_Decreasing_clicked( ) {
  QSettings settings( QSettings::IniFormat, QSettings::UserScope,
                      QApplication::organizationName( ), QApplication::applicationName( ) );
  settings.beginGroup( "waveform" );
  sortingKind = SortingKind::DECREASING;
  settings.setValue( "sortingType", static_cast< int >( sortingKind ) );
  settings.endGroup( );

  showWaveform( );
}

void SimpleWaveform::on_pushButton_Copy_clicked( ) {
  QSize s = chart.size( ).toSize( );
  QPixmap p( s );
  p.fill( Qt::white );
  QPainter painter;
  painter.begin( &p );
  painter.setRenderHint( QPainter::Antialiasing );
  chart.paint( &painter, nullptr, nullptr ); /* This gives 0 items in 1 group */
  chartView->render( &painter ); /* m_view has app->chart() in it, and this one gives right image */
  qDebug( ) << "Copied";
  painter.end( );
  QMimeData *d = new QMimeData( );
  d->setImageData( p );
  QApplication::clipboard( )->setMimeData( d, QClipboard::Clipboard );
}
