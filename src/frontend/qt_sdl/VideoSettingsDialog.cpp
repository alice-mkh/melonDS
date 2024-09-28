/*
    Copyright 2016-2023 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <stdio.h>
#include <QFileDialog>
#include <QtGlobal>

#include "types.h"
#include "Platform.h"
#include "Config.h"
#include "GPU.h"

#include "VideoSettingsDialog.h"
#include "ui_VideoSettingsDialog.h"


inline bool UsesGL()
{
    return (Config::ScreenUseGL != 0) || (Config::_3DRenderer != renderer3D_Software);
}

VideoSettingsDialog* VideoSettingsDialog::currentDlg = nullptr;

void VideoSettingsDialog::setEnabled()
{
    bool softwareRenderer = Config::_3DRenderer == renderer3D_Software;
    ui->cbGLDisplay->setEnabled(softwareRenderer);
    ui->cbSoftwareThreaded->setEnabled(softwareRenderer);
    ui->cbxGLResolution->setEnabled(!softwareRenderer);
    ui->cbBetterPolygons->setEnabled(Config::_3DRenderer == renderer3D_OpenGL);
    ui->cbxComputeHiResCoords->setEnabled(Config::_3DRenderer == renderer3D_OpenGLCompute);
}

VideoSettingsDialog::VideoSettingsDialog(QWidget* parent) : QDialog(parent), ui(new Ui::VideoSettingsDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

    oldRenderer = Config::_3DRenderer;
    oldGLDisplay = Config::ScreenUseGL;
    oldVSync = Config::ScreenVSync;
    oldVSyncInterval = Config::ScreenVSyncInterval;
    oldSoftThreaded = Config::Threaded3D;
    oldGLScale = Config::GL_ScaleFactor;
    oldGLBetterPolygons = Config::GL_BetterPolygons;
    oldHiresCoordinates = Config::GL_HiresCoordinates;

    grp3DRenderer = new QButtonGroup(this);
    grp3DRenderer->addButton(ui->rb3DSoftware, renderer3D_Software);
    grp3DRenderer->addButton(ui->rb3DOpenGL,   renderer3D_OpenGL);
    grp3DRenderer->addButton(ui->rb3DCompute,  renderer3D_OpenGLCompute);
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    connect(grp3DRenderer, SIGNAL(buttonClicked(int)), this, SLOT(onChange3DRenderer(int)));
#else
    connect(grp3DRenderer, SIGNAL(idClicked(int)), this, SLOT(onChange3DRenderer(int)));
#endif
    grp3DRenderer->button(Config::_3DRenderer)->setChecked(true);

#ifndef OGLRENDERER_ENABLED
    ui->rb3DOpenGL->setEnabled(false);
#endif

    ui->cbGLDisplay->setChecked(Config::ScreenUseGL != 0);

    ui->cbVSync->setChecked(Config::ScreenVSync != 0);
    ui->sbVSyncInterval->setValue(Config::ScreenVSyncInterval);

    ui->cbSoftwareThreaded->setChecked(Config::Threaded3D != 0);

    for (int i = 1; i <= 16; i++)
        ui->cbxGLResolution->addItem(QString("%1x native (%2x%3)").arg(i).arg(256*i).arg(192*i));
    ui->cbxGLResolution->setCurrentIndex(Config::GL_ScaleFactor-1);

    ui->cbBetterPolygons->setChecked(Config::GL_BetterPolygons != 0);
    ui->cbxComputeHiResCoords->setChecked(Config::GL_HiresCoordinates != 0);

    if (!Config::ScreenVSync)
        ui->sbVSyncInterval->setEnabled(false);
    setVsyncControlEnable(UsesGL());

    setEnabled();
}

VideoSettingsDialog::~VideoSettingsDialog()
{
    delete ui;
}

void VideoSettingsDialog::on_VideoSettingsDialog_accepted()
{
    Config::Save();

    closeDlg();
}

void VideoSettingsDialog::on_VideoSettingsDialog_rejected()
{
    bool old_gl = UsesGL();

    Config::_3DRenderer = oldRenderer;
    Config::ScreenUseGL = oldGLDisplay;
    Config::ScreenVSync = oldVSync;
    Config::ScreenVSyncInterval = oldVSyncInterval;
    Config::Threaded3D = oldSoftThreaded;
    Config::GL_ScaleFactor = oldGLScale;
    Config::GL_BetterPolygons = oldGLBetterPolygons;
    Config::GL_HiresCoordinates = oldHiresCoordinates;

    emit updateVideoSettings(old_gl != UsesGL());

    closeDlg();
}

void VideoSettingsDialog::setVsyncControlEnable(bool hasOGL)
{
    ui->cbVSync->setEnabled(hasOGL);
    ui->sbVSyncInterval->setEnabled(hasOGL);
}

void VideoSettingsDialog::onChange3DRenderer(int renderer)
{
    bool old_gl = UsesGL();

    Config::_3DRenderer = renderer;

    setEnabled();

    emit updateVideoSettings(old_gl != UsesGL());
}

void VideoSettingsDialog::on_cbGLDisplay_stateChanged(int state)
{
    bool old_gl = UsesGL();

    Config::ScreenUseGL = (state != 0);

    setVsyncControlEnable(UsesGL());

    emit updateVideoSettings(old_gl != UsesGL());
}

void VideoSettingsDialog::on_cbVSync_stateChanged(int state)
{
    bool vsync = (state != 0);
    ui->sbVSyncInterval->setEnabled(vsync);
    Config::ScreenVSync = vsync;
    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_sbVSyncInterval_valueChanged(int val)
{
    Config::ScreenVSyncInterval = val;
    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbSoftwareThreaded_stateChanged(int state)
{
    Config::Threaded3D = (state != 0);

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbxGLResolution_currentIndexChanged(int idx)
{
    // prevent a spurious change
    if (ui->cbxGLResolution->count() < 16) return;

    Config::GL_ScaleFactor = idx+1;

    setVsyncControlEnable(UsesGL());

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbBetterPolygons_stateChanged(int state)
{
    Config::GL_BetterPolygons = (state != 0);

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbxComputeHiResCoords_stateChanged(int state)
{
    Config::GL_HiresCoordinates = (state != 0);

    emit updateVideoSettings(false);
}
