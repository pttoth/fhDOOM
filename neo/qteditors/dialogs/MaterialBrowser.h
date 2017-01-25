/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 2016 Johannes Ohlemacher (http://github.com/eXistence/fhDOOM)

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#pragma once

#include "../precompiled.h"
#include <qdialog.h>
#include <qtabwidget.h>
#include <qtimer.h>

#include "DeclModel.h"

class QTreeView;
class QListWidget;
class QSortFilterProxyModel;
class fhMaterialModel;
class fhRenderWidget;
class idGLDrawable;


class fhDeclPreviewWidget : public QWidget {
public:
	explicit fhDeclPreviewWidget( QWidget* parent = nullptr )
		: QWidget( parent ) {
	}

	virtual void setDecl( const char* declname ) = 0;
};

class fhAbstractContentBrowser : public QDialog {
	Q_OBJECT
public:
	explicit fhAbstractContentBrowser( QWidget* parent = nullptr );
	~fhAbstractContentBrowser();

protected:
	void init( QSortFilterProxyModel* proxyModel, fhDeclPreviewWidget* previewWidget = nullptr );

	virtual void onItemSelected( const char* name );
	virtual QIcon createIcon( const idDecl* decl );

	QTreeView* tree;
	QListWidget* list;

private:
	void onTreeItemSelected( const QModelIndex& index );
	fhDeclPreviewWidget* previewWidget;
	QMap<QString, QIcon> iconCache;
};



class fhViewPort : public fhDeclPreviewWidget {
public:
	explicit fhViewPort( idGLDrawable* drawable, QWidget* parent = nullptr );
	~fhViewPort();

	virtual void setDecl( const char* declname ) override;

	void setUpdateRate( unsigned hz );

private:
	idGLDrawable* drawable;
	fhRenderWidget* renderWidget;
	QTimer timer;
};



template<typename TDataType, int TDeclEnum>
class fhDeclBrowser : public fhAbstractContentBrowser {
public:
	using Model = fhDeclModel<TDataType, TDeclEnum>;

	fhDeclBrowser( QWidget* parent, bool smartDecls = false ) {
		model = new Model();
		model->populate( smartDecls );
		QSortFilterProxyModel *proxyModel = new fhDeclFilterProxyModel<TDataType>( this );
		proxyModel->setSourceModel( model );

		init( proxyModel, nullptr );
	}

	~fhDeclBrowser() {
	}

private:
	Model* model;
};


class fhMaterialBrowser : public fhAbstractContentBrowser {
	Q_OBJECT

public:
	fhMaterialBrowser( QWidget* parent );
	~fhMaterialBrowser();

protected:
	virtual void onItemSelected( const char* name ) override;
	virtual QIcon createIcon( const idDecl* decl ) override;
private:
	void onTreeItemSelected( const QModelIndex& index );

	fhViewPort* viewPort;
	fhMaterialModel* materialModel;
};


class fhContentBrowser : public QTabWidget {
public:
	fhContentBrowser() {
		this->addTab( new fhMaterialBrowser( this ), "Materials" );
		this->addTab( new fhDeclBrowser<idDeclParticle, DECL_PARTICLE>( this, true ), "Particles" );
		this->addTab( new fhDeclBrowser<idSoundShader, DECL_SOUND>( this, true ), "Sound" );
		this->addTab( new fhDeclBrowser<idDeclFX, DECL_FX>( this, true ), "FX" );
		this->addTab( new fhDeclBrowser<idDeclAF, DECL_AF>( this, true ), "AF" );
		this->addTab( new fhDeclBrowser<idDeclEntityDef, DECL_ENTITYDEF>( this, true ), "Entities" );
		this->addTab( new fhDeclBrowser<idDeclModelDef, DECL_MODELDEF>( this, true ), "Models" );
		this->addTab( new fhDeclBrowser<idDeclSkin, DECL_SKIN>( this ), "Skins" );
	}
};