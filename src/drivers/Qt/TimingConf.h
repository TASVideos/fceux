// TimingConf.h
//

#pragma once

#include <QWidget>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QFrame>
#include <QGroupBox>
#include <QTreeView>
#include <QTreeWidget>

#include "Qt/main.h"

class TimingConfDialog_t : public QDialog
{
   Q_OBJECT

	public:
		TimingConfDialog_t(QWidget *parent = 0);
		~TimingConfDialog_t(void);

	protected:
		void closeEvent(QCloseEvent *event);

		QCheckBox  *emuPrioCtlEna;
		QComboBox  *emuSchedPolicyBox;
		QSlider    *emuSchedPrioSlider;
		QSlider    *emuSchedNiceSlider;
		QLabel     *emuSchedPrioLabel;

	private:
		void  updatePolicyBox(void);
		void  updateSliderLimits(void);
		void  updateSliderValues(void);

   public slots:
      void closeWindow(void);
	private slots:
		void emuSchedNiceChange( int val );
		void emuSchedPrioChange( int val );
		void emuSchedPolicyChange( int index );

};