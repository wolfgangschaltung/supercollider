/************************************************************************
*
* Copyright 2010-2011 Jakob Leben (jakob.leben@gmail.com)
*
* This file is part of Qt GUI for SuperCollider.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
************************************************************************/

#include "QcScopeShm.h"
#include "../QcWidgetFactory.h"
#include "../debug.h"

#include <QPainter>
#include <QTimer>

static QcWidgetFactory<QcScopeShm> factory;

QcScopeShm::QcScopeShm() :
  _srvPort(-1),
  _scopeIndex(-1),
  _shmClient(0),
  _running(false),
  _data(0),
  _availableFrames(0),
  xOffset( 0.f ),
  yOffset( 0.f ),
  xZoom( 1.f ),
  yZoom( 1.f ),
  _style( 0 ),
  _bkg( QColor(0,0,0) )
{
  timer = new QTimer( this );
  timer->setInterval( 50 );
  setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
  connect( timer, SIGNAL( timeout() ), this, SLOT( updateScope() ) );
}

QcScopeShm::~QcScopeShm()
{
  stop();
}

void QcScopeShm::setServerPort( int port )
{
  if( _running ) {
    qcWarningMsg( "QScope: Can not change server port while running!" );
    return;
  }

  _srvPort = port;
}

void QcScopeShm::setBufferNumber( int n )
{
  if( _running ) {
    // TODO: release used reader?
    initScopeReader( _shmClient, n );
  }
  _scopeIndex = n;
}

void QcScopeShm::setWaveColors( const VariantList & newColors )
{
  colors.clear();
  Q_FOREACH( QVariant var, newColors.data ) {
    QColor color = var.value<QColor>();
    if( !color.isValid() )
      colors.append( QColor( 0,0,0 ) );
    else
      colors.append( color );
  }
}

int QcScopeShm::updateInterval() const {
  return timer->interval();
}

void QcScopeShm::setUpdateInterval( int interval ) {
  timer->setInterval( qMax(0, interval) );
}

void QcScopeShm::start()
{
  if( _running ) return;
  if( _srvPort < 0 || _scopeIndex < 0 ) return;

  connectSharedMemory( _srvPort );
  if( !_shmClient ) {
    stop();
    return;
  }

  initScopeReader( _shmClient, _scopeIndex );

  timer->start();

  _running = true;
}

void QcScopeShm::stop()
{
  // TODO: release used reader?

  delete _shmClient;
  _shmClient = 0;

  timer->stop();

  _running = false;
}

void QcScopeShm::updateScope()
{
  bool valid = _shmReader.valid();
  //qcDebugMsg(1, tr("valid = %1").arg(valid));
  if(!valid) return;

  bool ok = _shmReader.pull( _availableFrames );
  //qcDebugMsg(1, tr("Got %1 frames").arg(_availableFrames) );
  if(ok) {
    _data = _shmReader.data();
    update();
  }
}

void QcScopeShm::paintEvent ( QPaintEvent * event )
{
  Q_UNUSED( event );

  QPainter p( this );
  QRect area = rect();
  p.fillRect( area, _bkg );

  if( !_running || !_availableFrames ) return;

  int chanCount = _shmReader.channels();
  int maxFrames = _shmReader.max_frames();

  if( _style == 0 )
    paint1D( false, chanCount, maxFrames, _availableFrames, p );
  else if( _style == 1 )
    paint1D( true, chanCount, maxFrames, _availableFrames, p );
  else if( _style == 2 )
    paint2D( chanCount, maxFrames, _availableFrames, p );
}

void QcScopeShm::paint1D( bool overlapped, int chanCount, int maxFrames, int frameCount, QPainter & painter )
{
  if( _availableFrames < 2 ) return;

  //qcDebugMsg( 0, tr("Drawing: data %1 / channels %2 / max-size %3").arg(_data!=0).arg(chanCount).arg(maxFrames) );

  QRect area = rect();
  float yRatio = - yZoom * area.height() * 0.5;
  if( !overlapped ) yRatio /= chanCount;
  float yHeight = area.height();
  if( !overlapped ) yHeight /= chanCount;

  if( frameCount < area.width() )
  {
    float xRatio = xZoom * area.width() / (frameCount-1);

    for( int ch = 0; ch < chanCount; ch++ ) {
      float *frameData = _data + (ch * maxFrames); //frame vector
      float yOrigin = yHeight * (overlapped ? 0.5 : ch + 0.5);
      QColor color = ( ch < colors.count() ? colors[ch] : QColor(255,255,255) );

      painter.save();
      painter.translate( area.x(), area.y() + yOrigin );
      painter.scale( xRatio, yRatio );
      painter.setPen(color);

      QPainterPath path;
      path.moveTo( xOffset, frameData[0] );
      for( int f = 1; f < frameCount; ++f )
        path.lineTo( xOffset + f, frameData[f] );

      painter.drawPath(path);

      painter.restore();
    }
  }
  else
  {
    int w = area.width();
    float ppf = ((float) w) / frameCount;

    for( int ch = 0; ch < chanCount; ch++ )
    {
      float *frameData = _data + (ch * maxFrames); //frame vector
      float yOrigin = yHeight * (overlapped ? 0.5 : ch + 0.5);
      QColor color = ( ch < colors.count() ? colors[ch] : QColor(255,255,255) );

      painter.save();
      painter.translate( area.x(), area.y() + yOrigin );
      painter.scale( 1, yRatio );
      painter.setPen(color);

      int p=0, f=1; // pixel, frame
      while( p++ < w )
      {
        float min, max;
        // include the previous frame to ensure continuity
        min = max = frameData[f-1];

        for(; f * ppf < p; ++f)
        {
          float d = frameData[f];
          if( d < min ) min = d;
          else if( d > max ) max = d;
        }

        qreal pix = p-1;
        QLineF line( pix, min, pix, max );
        painter.drawLine( line );
      }

      painter.restore();
    }
  }
}

void QcScopeShm::paint2D( int chanCount, int maxFrames, int frameCount, QPainter & painter )
{
  QColor color = colors.count() ? colors[0] : QColor(255,255,255);

  QRect area = rect();
  int minSize = qMin( area.width(), area.height() );
  // NOTE: use yZoom for both axis, since both represent value, as opposed to index
  float xRatio = yZoom * minSize * 0.5;
  float yRatio = -yZoom * minSize * 0.5;
  QPoint center = area.center();

  painter.setPen(color);
  painter.translate( center.x(), center.y() );
  painter.scale( xRatio, yRatio );

  QPainterPath path;

  if( chanCount >= 2 )
  {
    float *data1 = _data;
    float *data2 = _data + maxFrames;

    path.moveTo( data1[0], data2[0] );
    for( int f = 1; f < frameCount; ++f )
      path.lineTo( data1[f], data2[f] );
  }
  else
  {
    float *data1 = _data;
    path.moveTo( data1[0], 0.f );
    for( int f = 1; f < frameCount; ++f )
      path.lineTo( data1[f], 0.f );
  }

  painter.drawPath(path);
}

void QcScopeShm::connectSharedMemory( int port )
{
  try {
      server_shared_memory_client * client = new server_shared_memory_client(port);
      _shmClient = client;
      qcDebugMsg(1,"Shared memory connected");
  } catch (std::exception & e) {
      _shmClient = 0;
      qcErrorMsg(QString("Cannot connect to shared memory: %1").arg(e.what()) );
  }
}

void QcScopeShm::initScopeReader( server_shared_memory_client *shm, int index )
{
  _shmReader = shm->get_scope_buffer_reader( index );
  qcDebugMsg(1,QString("Initialized scope buffer reader for index %1.").arg(index));
}
