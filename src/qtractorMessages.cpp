// qtractorMessages.cpp
//
/****************************************************************************
   Copyright (C) 2005-2006, rncbc aka Rui Nuno Capela. All rights reserved.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*****************************************************************************/

#include "qtractorAbout.h"
#include "qtractorMessages.h"

#include "qtractorMainForm.h"

#include <QSocketNotifier>
#include <QTextEdit>
#include <QTextCursor>
#include <QTextBlock>
#include <QScrollBar>
#include <QDateTime>
#include <QIcon>

#if !defined(WIN32)
#include <unistd.h>
#endif

// The default maximum number of message lines.
#define QTRACTOR_MESSAGES_MAXLINES  1000

// Notification pipe descriptors
#define QTRACTOR_MESSAGES_FDNIL    -1
#define QTRACTOR_MESSAGES_FDREAD    0
#define QTRACTOR_MESSAGES_FDWRITE   1


//-------------------------------------------------------------------------
// qtractorMessages - Messages log dockable window.
//

// Constructor.
qtractorMessages::qtractorMessages ( QWidget *pParent )
	: QDockWidget(pParent)
{
	// Surely a name is crucial (e.g.for storing geometry settings)
	QDockWidget::setObjectName("qtractorMessages");

	// Intialize stdout capture stuff.
	m_pStdoutNotifier = NULL;
	m_fdStdout[QTRACTOR_MESSAGES_FDREAD]  = QTRACTOR_MESSAGES_FDNIL;
	m_fdStdout[QTRACTOR_MESSAGES_FDWRITE] = QTRACTOR_MESSAGES_FDNIL;

	// Create local text view widget.
	m_pTextView = new QTextEdit(this);
//  QFont font(m_pTextView->font());
//  font.setFamily("Fixed");
//  m_pTextView->setFont(font);
	m_pTextView->setLineWrapMode(QTextEdit::NoWrap);
	m_pTextView->setReadOnly(true);
	m_pTextView->setUndoRedoEnabled(false);
//	m_pTextView->setTextFormat(Qt::LogText);

	// Initialize default message limit.
	m_iMessagesLines = 0;
	setMessagesLimit(QTRACTOR_MESSAGES_MAXLINES);

	// Prepare the dockable window stuff.
	QDockWidget::setWidget(m_pTextView);
	QDockWidget::setFeatures(QDockWidget::AllDockWidgetFeatures);
	QDockWidget::setAllowedAreas(
		Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
	// Some specialties to this kind of dock window...
	QDockWidget::setMinimumHeight(120);

	// Finally set the default caption and tooltip.
	const QString& sCaption = tr("Messages");
	QDockWidget::setWindowTitle(sCaption);
	QDockWidget::setWindowIcon(QIcon(":/icons/viewMessages.png"));
	QDockWidget::setToolTip(sCaption);
}


// Destructor.
qtractorMessages::~qtractorMessages (void)
{
	// No more notifications.
	if (m_pStdoutNotifier)
		delete m_pStdoutNotifier;

	// No need to delete child widgets, Qt does it all for us.
}


// Just about to notify main-window that we're closing.
void qtractorMessages::closeEvent ( QCloseEvent * /*pCloseEvent*/ )
{
#ifdef CONFIG_DEBUG
	fprintf(stderr, "qtractorMessages::closeEvent()\n");
#endif

	QDockWidget::hide();

	qtractorMainForm *pMainForm = qtractorMainForm::getInstance();
	if (pMainForm)
		pMainForm->stabilizeForm();
}


// Own stdout/stderr socket notifier slot.
void qtractorMessages::stdoutNotify ( int fd )
{
#if !defined(WIN32)
	char achBuffer[1024];
	int  cchBuffer = ::read(fd, achBuffer, sizeof(achBuffer) - 1);
	if (cchBuffer > 0) {
		achBuffer[cchBuffer] = (char) 0;
		appendStdoutBuffer(achBuffer);
	}
#endif
}


// Stdout buffer handler -- now splitted by complete new-lines...
void qtractorMessages::appendStdoutBuffer ( const QString& s )
{
	m_sStdoutBuffer.append(s);

	int iLength = m_sStdoutBuffer.lastIndexOf('\n') + 1;
	if (iLength > 0) {
		QString sTemp = m_sStdoutBuffer.left(iLength);
		m_sStdoutBuffer.remove(0, iLength);
		QStringList list = sTemp.split('\n');
		QStringListIterator iter(list);
		while (iter.hasNext())
			appendMessagesText(iter.next());
	}
}


// Stdout flusher -- show up any unfinished line...
void qtractorMessages::flushStdoutBuffer (void)
{
	if (!m_sStdoutBuffer.isEmpty()) {
		appendMessagesText(m_sStdoutBuffer);
		m_sStdoutBuffer.truncate(0);
	}
}


// Stdout capture accessors.
bool qtractorMessages::isCaptureEnabled (void) const
{
	return (bool) (m_pStdoutNotifier != NULL);
}

void qtractorMessages::setCaptureEnabled ( bool bCapture )
{
	// Flush current buffer.
	flushStdoutBuffer();

#if !defined(WIN32)
	// Destroy if already enabled.
	if (!bCapture && m_pStdoutNotifier) {
		delete m_pStdoutNotifier;
		m_pStdoutNotifier = NULL;
		// Close the notification pipes.
		if (m_fdStdout[QTRACTOR_MESSAGES_FDREAD] != QTRACTOR_MESSAGES_FDNIL) {
			::close(m_fdStdout[QTRACTOR_MESSAGES_FDREAD]);
			m_fdStdout[QTRACTOR_MESSAGES_FDREAD]  = QTRACTOR_MESSAGES_FDNIL;
		}
		if (m_fdStdout[QTRACTOR_MESSAGES_FDREAD] != QTRACTOR_MESSAGES_FDNIL) {
			::close(m_fdStdout[QTRACTOR_MESSAGES_FDREAD]);
			m_fdStdout[QTRACTOR_MESSAGES_FDREAD]  = QTRACTOR_MESSAGES_FDNIL;
		}
	}
	// Are we going to make up the capture?
	if (bCapture && m_pStdoutNotifier == NULL && ::pipe(m_fdStdout) == 0) {
		::dup2(m_fdStdout[QTRACTOR_MESSAGES_FDWRITE], STDOUT_FILENO);
		::dup2(m_fdStdout[QTRACTOR_MESSAGES_FDWRITE], STDERR_FILENO);
		m_pStdoutNotifier = new QSocketNotifier(
			m_fdStdout[QTRACTOR_MESSAGES_FDREAD], QSocketNotifier::Read, this);
		QObject::connect(m_pStdoutNotifier,
			SIGNAL(activated(int)),
			SLOT(stdoutNotify(int)));
	}
#endif
}


// Message font accessors.
QFont qtractorMessages::messagesFont (void) const
{
	return m_pTextView->font();
}

void qtractorMessages::setMessagesFont( const QFont& font )
{
	m_pTextView->setFont(font);
}


// Maximum number of message lines accessors.
int qtractorMessages::messagesLimit (void) const
{
	return m_iMessagesLimit;
}

void qtractorMessages::setMessagesLimit ( int iMessagesLimit )
{
	m_iMessagesLimit = iMessagesLimit;
	m_iMessagesHigh  = iMessagesLimit + (iMessagesLimit >> 2);
}


// The main utility methods.
void qtractorMessages::appendMessages ( const QString& s )
{
	appendMessagesColor(s, "#999999");
}

void qtractorMessages::appendMessagesColor ( const QString& s, const QString &c )
{
	appendMessagesText("<font color=\"" + c + "\">"
		+ QTime::currentTime().toString("hh:mm:ss.zzz")
		+ ' ' + s + "</font>");
}

void qtractorMessages::appendMessagesText ( const QString& s )
{
    // Check for message line limit...
    if (m_iMessagesLines > m_iMessagesHigh) {
		m_pTextView->setUpdatesEnabled(false);
		QTextCursor textCursor(m_pTextView->document()->begin());
		while (m_iMessagesLines > m_iMessagesLimit) {
			// Move cursor extending selection
			// from start to next line-block...
			textCursor.movePosition(
				QTextCursor::NextBlock, QTextCursor::KeepAnchor);
			m_iMessagesLines--;
		}
		// Remove the excessive line-blocks...
		textCursor.removeSelectedText();
		m_pTextView->setUpdatesEnabled(true);
    }

	// Count always as a new line out there...
	m_pTextView->append(s);
	m_iMessagesLines++;
}


// History reset.
void qtractorMessages::clear (void)
{
	m_iMessagesLines = 0;
	m_pTextView->clear();
}


// end of qtractorMessages.cpp
