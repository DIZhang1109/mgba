/* Copyright (c) 2013-2015 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "MemoryModel.h"

#include "GameController.h"

#include <QFontMetrics>
#include <QPainter>
#include <QScrollBar>
#include <QSlider>
#include <QWheelEvent>

extern "C" {
#include "gba/memory.h"
}

using namespace QGBA;

MemoryModel::MemoryModel(QWidget* parent)
	: QAbstractScrollArea(parent)
	, m_cpu(nullptr)
	, m_top(0)
	, m_align(1)
	, m_selection(0, 0)
	, m_selectionAnchor(0)
{
	m_font.setFamily("Source Code Pro");
	m_font.setStyleHint(QFont::Monospace);
	m_font.setPointSize(12);
	QFontMetrics metrics(m_font);
	m_cellHeight = metrics.height();
	m_letterWidth = metrics.averageCharWidth();

	for (int i = 0; i < 256; ++i) {
		QStaticText str(QString("%0").arg(i, 2, 16, QChar('0')).toUpper());
		str.prepare(QTransform(), m_font);
		m_staticNumbers.append(str);
	}

	for (int i = 0; i < 128; ++i) {
		QChar c(i);
		if (!c.isPrint()) {
			c = '.';
		}
		QStaticText str = QStaticText(QString(c));
		str.prepare(QTransform(), m_font);
		m_staticAscii.append(str);
	}

	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	m_margins = QMargins(metrics.width("0FFFFFF0 ") + 3, m_cellHeight + 1, metrics.width(" AAAAAAAAAAAAAAAA") + 3, 0);
	m_cellSize = QSizeF((viewport()->size().width() - (m_margins.left() + m_margins.right())) / 16.0, m_cellHeight);

	connect(verticalScrollBar(), &QSlider::sliderMoved, [this](int position) {
		m_top = position;
		update();
	});

	setRegion(0, 0x10000000, tr("All"));
}

void MemoryModel::setController(GameController* controller) {
	m_cpu = controller->thread()->cpu;
}

void MemoryModel::setRegion(uint32_t base, uint32_t size, const QString& name) {
	m_top = 0;
	m_base = base;
	m_size = size;
	m_regionName = name;
	m_regionName.prepare(QTransform(), m_font);
	verticalScrollBar()->setRange(0, (size >> 4) + 1 - viewport()->size().height() / m_cellHeight);
	verticalScrollBar()->setValue(0);
	viewport()->update();
}

void MemoryModel::setAlignment(int width) {
	if (width != 1 && width != 2 && width != 4) {
		return;
	}
	m_align = width;
	viewport()->update();
}

void MemoryModel::jumpToAddress(const QString& hex) {
	bool ok = false;
	uint32_t i = hex.toInt(&ok, 16);
	if (ok) {
		jumpToAddress(i);
	}
}

void MemoryModel::jumpToAddress(uint32_t address) {
	if (address >= 0x10000000) {
		return;
	}
	if (address < m_base || address >= m_base + m_size) {
		setRegion(0, 0x10000000, tr("All"));
	}
	m_top = (address - m_base) / 16;
	boundsCheck();
	verticalScrollBar()->setValue(m_top);
}

void MemoryModel::resizeEvent(QResizeEvent*) {
	m_cellSize = QSizeF((viewport()->size().width() - (m_margins.left() + m_margins.right())) / 16.0, m_cellHeight);
	verticalScrollBar()->setRange(0, (m_size >> 4) + 1 - viewport()->size().height() / m_cellHeight);
	boundsCheck();
}

void MemoryModel::paintEvent(QPaintEvent* event) {
	QPainter painter(viewport());
	painter.setFont(m_font);
	QChar c0('0');
	QSizeF letterSize = QSizeF(m_letterWidth, m_cellHeight);
	painter.drawStaticText(QPointF((m_margins.left() - m_regionName.size().width() - 1) / 2.0, 0), m_regionName);
	painter.drawText(QRect(QPoint(viewport()->size().width() - m_margins.right(), 0), QSize(m_margins.right(), m_margins.top())), Qt::AlignHCenter, tr("ASCII"));
	for (int x = 0; x < 16; ++x) {
		painter.drawText(QRectF(QPointF(m_cellSize.width() * x + m_margins.left(), 0), m_cellSize), Qt::AlignHCenter, QString::number(x, 16).toUpper());
	}
	int height = (viewport()->size().height() - m_cellHeight) / m_cellHeight;
	for (int y = 0; y < height; ++y) {
		int yp = m_cellHeight * y + m_margins.top();
		QString data = QString("%0").arg((y + m_top) * 16 + m_base, 8, 16, c0).toUpper();
		painter.drawText(QRectF(QPointF(0, yp), QSizeF(m_margins.left(), m_cellHeight)), Qt::AlignHCenter, data);
		switch (m_align) {
		case 2:
			for (int x = 0; x < 16; x += 2) {
				uint32_t address = (y + m_top) * 16 + x + m_base;
				if (isInSelection(address)) {
					painter.fillRect(QRectF(QPointF(m_cellSize.width() * x + m_margins.left(), yp), QSizeF(m_cellSize.width() * 2, m_cellSize.height())), Qt::lightGray);
				}
				uint16_t b = m_cpu->memory.load16(m_cpu, address, nullptr);
				painter.drawStaticText(QPointF(m_cellSize.width() * (x + 1.0) - 2 * m_letterWidth + m_margins.left(), yp), m_staticNumbers[(b >> 8) & 0xFF]);
				painter.drawStaticText(QPointF(m_cellSize.width() * (x + 1.0) + m_margins.left(), yp), m_staticNumbers[b & 0xFF]);
			}
			break;
		case 4:
			for (int x = 0; x < 16; x += 4) {
				uint32_t address = (y + m_top) * 16 + x + m_base;
				if (isInSelection(address)) {
					painter.fillRect(QRectF(QPointF(m_cellSize.width() * x + m_margins.left(), yp), QSizeF(m_cellSize.width() * 4, m_cellSize.height())), Qt::lightGray);
				}
				uint32_t b = m_cpu->memory.load32(m_cpu, address, nullptr);
				painter.drawStaticText(QPointF(m_cellSize.width() * (x + 2.0) - 4 * m_letterWidth + m_margins.left(), yp), m_staticNumbers[(b >> 24) & 0xFF]);
				painter.drawStaticText(QPointF(m_cellSize.width() * (x + 2.0) - 2 * m_letterWidth + m_margins.left(), yp), m_staticNumbers[(b >> 16) & 0xFF]);
				painter.drawStaticText(QPointF(m_cellSize.width() * (x + 2.0) + m_margins.left(), yp), m_staticNumbers[(b >> 8) & 0xFF]);
				painter.drawStaticText(QPointF(m_cellSize.width() * (x + 2.0) + 2 * m_letterWidth + m_margins.left(), yp), m_staticNumbers[b & 0xFF]);
			}
			break;
		case 1:
		default:
			for (int x = 0; x < 16; ++x) {
				uint32_t address = (y + m_top) * 16 + x + m_base;
				if (isInSelection(address)) {
					painter.fillRect(QRectF(QPointF(m_cellSize.width() * x + m_margins.left(), yp), m_cellSize), Qt::lightGray);
				}
				uint8_t b = m_cpu->memory.load8(m_cpu, address, nullptr);
				painter.drawStaticText(QPointF(m_cellSize.width() * (x + 0.5) - m_letterWidth + m_margins.left(), yp), m_staticNumbers[b]);
			}
			break;
		}
		for (int x = 0; x < 16; ++x) {
			uint8_t b = m_cpu->memory.load8(m_cpu, (y + m_top) * 16 + x + m_base, nullptr);
			painter.drawStaticText(QPointF(viewport()->size().width() - (16 - x) * m_margins.right() / 17.0 - m_letterWidth * 0.5, yp), b < 0x80 ? m_staticAscii[b] : m_staticAscii[0]);
		}
	}
	painter.drawLine(m_margins.left(), 0, m_margins.left(), viewport()->size().height());
	painter.drawLine(viewport()->size().width() - m_margins.right(), 0, viewport()->size().width() - m_margins.right(), viewport()->size().height());
	painter.drawLine(0, m_margins.top(), viewport()->size().width(), m_margins.top());
}

void MemoryModel::wheelEvent(QWheelEvent* event) {
	m_top -= event->angleDelta().y() / 8;
	boundsCheck();
	event->accept();
	verticalScrollBar()->setValue(m_top);
	update();
}

void MemoryModel::mousePressEvent(QMouseEvent* event) {
	if (event->x() < m_margins.left() || event->y() < m_margins.top() || event->x() > size().width() - m_margins.right()) {
		m_selection = qMakePair(0, 0);
		return;
	}

	QPoint position(event->pos() - QPoint(m_margins.left(), m_margins.top()));
	uint32_t address = int(position.x() / m_cellSize.width()) + (int(position.y() / m_cellSize.height()) + m_top) * 16 + m_base;
	m_selectionAnchor = address & ~(m_align - 1);
	m_selection = qMakePair(m_selectionAnchor, m_selectionAnchor + m_align);
	viewport()->update();
}

void MemoryModel::mouseMoveEvent(QMouseEvent* event) {
	if (event->x() < m_margins.left() || event->y() < m_margins.top() || event->x() > size().width() - m_margins.right()) {
		return;
	}

	QPoint position(event->pos() - QPoint(m_margins.left(), m_margins.top()));
	uint32_t address = int(position.x() / m_cellSize.width()) + (int(position.y() / m_cellSize.height()) + m_top) * 16 + m_base;
	if ((address & ~(m_align - 1)) < m_selectionAnchor) {
		m_selection = qMakePair(address & ~(m_align - 1), m_selectionAnchor + m_align);
	} else {
		m_selection = qMakePair(m_selectionAnchor, (address & ~(m_align - 1)) + m_align);
	}
	viewport()->update();
}

void MemoryModel::boundsCheck() {
	if (m_top < 0) {
		m_top = 0;
	} else if (m_top > (m_size >> 4) + 1 - viewport()->size().height() / m_cellHeight) {
		m_top = (m_size >> 4) + 1 - viewport()->size().height() / m_cellHeight;
	}
}

bool MemoryModel::isInSelection(uint32_t address) {
	if (m_selection.first == m_selection.second) {
		return false;
	}
	if (m_selection.second <= (address | (m_align - 1))) {
		return false;
	}
	if (m_selection.first <= (address & ~(m_align - 1))) {
		return true;
	}
	return false;
}
