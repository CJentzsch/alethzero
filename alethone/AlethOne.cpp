/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file AlethOne.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "AlethOne.h"
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QButtonGroup>
#include <QDateTime>
#include <libethcore/Common.h>
#include <libethcore/ICAP.h>
#include <libethereum/Client.h>
#include <libwebthree/WebThree.h>
#include <libaleth/SendDialog.h>
#include "BuildInfo.h"
#include "ui_AlethOne.h"
using namespace std;
using namespace dev;
using namespace p2p;
using namespace eth;
using namespace aleth;
using namespace one;

AlethOne::AlethOne():
	m_ui(new Ui::AlethOne)
{
	g_logVerbosity = 1;

	setWindowFlags(Qt::Window);
	m_ui->setupUi(this);
	m_aleth.init(Aleth::Nothing, "AlethOne", "anon");

	auto g = new QButtonGroup(this);
	g->setExclusive(true);
	g->addButton(m_ui->noMining);

	auto translate = [](QString s) { return s == "cpu" ? "CPU" : s == "opencl" ? "GPU" : s + " Miner"; };
	for (QString i: m_slave.sealers())
#ifdef NDEBUG
	if (i != "cpu")
#endif
	{
		QToolButton* a = new QToolButton(this);
		a->setText(translate(i));
		a->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
		a->setCheckable(true);
		connect(a, &QToolButton::clicked, [=]()
		{
			if (m_ui->local->isChecked())
			{
				m_aleth.ethereum()->setSealer(i.toStdString());
				m_aleth.ethereum()->startMining();
			}
			else if (m_ui->pool->isChecked())
			{
				m_slave.setURL(m_ui->url->text());
				m_slave.setSealer(i);
				m_slave.start();
			}
			refresh();
		});
		g->addButton(a);
		m_ui->mine->addWidget(a);
	}

	m_ui->version->setText((c_network == eth::Network::Olympic ? "Olympic" : "Frontier") + QString(" AlethOne v") + QString::fromStdString(niceVersion(dev::Version)));
	m_ui->beneficiary->setText(QString::fromStdString(ICAP(m_aleth.keyManager().accounts().front()).encoded()));
	m_ui->sync->setAleth(&m_aleth);

	{
		QTimer* t = new QTimer(this);
		connect(t, SIGNAL(timeout()), SLOT(refresh()));
		t->start(1000);
	}

	m_ui->stack->adjustSize();
	m_ui->stack->setMinimumWidth(m_ui->stack->height() * 1.35);
	m_ui->stack->setMaximumWidth(m_ui->stack->height() * 1.5);

	readSettings();
}

AlethOne::~AlethOne()
{
	writeSettings();
	delete m_ui;
}

void AlethOne::refresh()
{
	m_ui->peers->setValue(m_aleth ? m_aleth.web3()->peerCount() : 0);
	if (m_aleth)
		cwarn << m_aleth.web3()->peerCount();
	bool s = m_aleth ? m_aleth.ethereum()->isSyncing() : false;
	pair<uint64_t, unsigned> gp = EthashAux::fullGeneratingProgress();
	m_ui->stack->setCurrentIndex(s ? 0 : 1);
	if (!s)
	{
		if (m_ui->noMining->isChecked())
		{
			m_ui->hashrate->setText("/");
			m_ui->underHashrate->setText("Not mining");
		}
		else
		{
			bool m;
			u256 r;
			if (m_aleth)
			{
				m = m_aleth.ethereum()->isMining();
				r = m_aleth.ethereum()->hashrate();
			}
			else
			{
				m = m_slave.isWorking();
				r = m ? m_slave.hashrate() : 0;
			}
			if (r > 0)
			{
				QStringList ss = QString::fromStdString(inUnits(r, { "hash/s", "Khash/s", "Mhash/s", "Ghash/s" })).split(" ");
				m_ui->hashrate->setText(ss[0]);
				m_ui->underHashrate->setText(ss[1]);
			}
			else if (gp.first != EthashAux::NotGenerating)
			{
				m_ui->hashrate->setText(QString("%1%").arg(gp.second));
				m_ui->underHashrate->setText(QString("Preparing").arg(gp.first));
			}
			else if (m)
			{
				m_ui->hashrate->setText("...");
				m_ui->underHashrate->setText("Preparing");
			}
			else
			{
				m_ui->hashrate->setText("...");
				m_ui->underHashrate->setText("Waiting");
			}
		}
	}
}

void AlethOne::readSettings()
{
	QSettings s("ethereum", "alethone");
	if (s.value("mode", "solo").toString() == "solo")
		m_ui->local->setChecked(true);
	else
		m_ui->pool->setChecked(true);
	m_ui->url->setText(s.value("url", "http://127.0.0.1:8545").toString());
}

void AlethOne::writeSettings()
{
	QSettings s("ethereum", "alethone");
	s.setValue("mode", m_ui->local->isChecked() ? "solo" : "pool");
	s.setValue("url", m_ui->url->text());
}

void AlethOne::on_noMining_clicked()
{
	if (m_aleth)
		m_aleth.ethereum()->stopMining();
	m_slave.stop();
	refresh();
}

void AlethOne::on_send_clicked()
{
	SendDialog sd(this, &m_aleth);
	sd.exec();
}

void AlethOne::on_local_toggled(bool _on)
{
	if (_on)
	{
		if (!m_aleth.open(Aleth::Bootstrap))
			m_ui->pool->setChecked(true);
		else
			m_aleth.installWatch(ChainChangedFilter, [=](LocalisedLogEntries const&){
				log(QString("New block #%1").arg(this->m_aleth.ethereum()->number()));
				u256 balance = 0;
				auto accounts = this->m_aleth.keyManager().accounts();
				for (auto const& a: accounts)
					balance += this->m_aleth.ethereum()->balanceAt(a);
				this->m_ui->balance->setText(QString("%1 across %2 accounts").arg(QString::fromStdString(formatBalance(balance))).arg(accounts.size()));
			});
	}
	else
		m_aleth.close();
}

void AlethOne::log(QString _s)
{
	QString s;
	s = QTime::currentTime().toString("hh:mm:ss") + " " + _s;
	m_ui->log->appendPlainText(s);
}
