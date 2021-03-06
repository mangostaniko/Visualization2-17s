#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <iostream>
#include <stdlib.h>
#include <time.h>

#include <QFileDialog>
#include <qmessagebox.h>
#include <QPainter>


MainWindow::MainWindow(QWidget *parent) :
        QMainWindow(parent),
        ui(new Ui::MainWindow)
{
	ui->setupUi(this);

	QLayout *layout = ui->controls->layout();
	layout->setAlignment(Qt::AlignTop);
	ui->controls->setLayout(layout);

	// enable multisampling for anti-aliasing
	auto format = QSurfaceFormat();
	format.setSamples(16);
	format.setVersion(3, 3);
	QSurfaceFormat::setDefaultFormat(format);

	glWidget = new GLWidget(this, this);
	ui->glLayout->addWidget(glWidget);


	connect(ui->actionOpen, SIGNAL(triggered()), this, SLOT(openFileAction()));
	connect(ui->actionClose, SIGNAL(triggered()), this, SLOT(closeAction()));

	ui->memSizeLCD->setPalette(Qt::darkBlue);
	ui->usedMemLCD->setPalette(Qt::darkGreen);
	ui->fpsLCD->setPalette(Qt::darkGreen);

}

MainWindow::~MainWindow()
{
	delete ui;
}

glm::vec3 MainWindow::randomPosInBoundingBox(glm::vec3 boundingBoxMin, glm::vec3 boundingBoxMax)
{
	float rx = ((boundingBoxMax.x - boundingBoxMin.x) * ((float) rand() / RAND_MAX)) + boundingBoxMin.x;
	float ry = ((boundingBoxMax.y - boundingBoxMin.y) * ((float) rand() / RAND_MAX)) + boundingBoxMin.y;
	float rz = ((boundingBoxMax.z - boundingBoxMin.z) * ((float) rand() / RAND_MAX)) + boundingBoxMin.z;

	return glm::vec3(rx, ry, rz);
}

void MainWindow::generateTestData(int numVertices, glm::vec3 boundingBoxMin, glm::vec3 boundingBoxMax)
{
	srand(time(NULL)); // change pseudorandom number generator seed

	datasetLines.clear();

	std::vector<glm::vec3> line1Positions;

	// GENERATE POSITIONS ALONG A SMOOTH LINE

	// we have a starting position and direction, take a step in that direction and store a new vertex position.
	// to make line curve smoothly
	// we have a target position we want to move towards, but we dont look there directly,
	// instead we add little bit of target direction slowly each time so that we curve slowly towards target.
	// when we are close enough to the target or at random chance we choose a new random target within bounding box.
	glm::vec3 currentPos = glm::vec3(boundingBoxMin); // where we are
	glm::vec3 targetPos = glm::vec3((boundingBoxMax+boundingBoxMin)/2.f); // where we want to go
	glm::vec3 currentDirection = glm::vec3(0,1,0); // where we are looking
	glm::vec3 targetDirection = glm::normalize(targetPos - currentPos); // where we want to look
	float stepSize = 0.01f; // lower for finer line resolution (more vertices)
	float curviness = 0.8f; // interpolation factor between current direction and target direction
	float targetChangeProbability = 0.07f;
	float minDistanceToTarget = 0.06f;

	for (int i = 0; i < numVertices; ++i) {

		// set a new random target if we are close enough to target or at random chance according to given probability
		if (glm::length(targetPos - currentPos) < minDistanceToTarget || ((float)rand() / RAND_MAX) <= targetChangeProbability) {
			targetPos = randomPosInBoundingBox(boundingBoxMin, boundingBoxMax);
		}

		// to make line curve smoothly add little bit of target direction slowly each time instead of looking directly at target
		// it may happen that the direction interpolation reaches target direction before target position is reached, but thats ok and adds variation
		targetDirection = glm::normalize(targetPos - currentPos);
		currentDirection = glm::normalize(curviness*currentDirection + (1-curviness)*targetDirection);

		currentPos += currentDirection*stepSize;

		line1Positions.push_back(glm::vec3(currentPos));
	}

	// adjust draw parameters for this dataset
	ui->spinBoxLineTriangleStripWidth->setValue(0.03f);
	ui->spinBoxLineWidthPercentageBlack->setValue(0.3f);
	ui->spinBoxLineWidthDepthCueingFactor->setValue(1.0f);
	ui->spinBoxLineHaloMaxDepth->setValue(0.02f);

	generateAdditionalLineVertexData(line1Positions);

	qDebug() << "Test line data generated:" << numVertices << "line vertices," << datasetLines[0].size() << "vertices after duplication for triangle strip drawing. Each vertex consists of 8 floats (3 pos, 3 direction to next, 2 uv for triangle strip drawing).";

	glWidget->initLineRenderMode(&datasetLines);
}

void MainWindow::openFileAction()
{
	QString filename = QFileDialog::getOpenFileName(this, "Open dataset file...", 0, tr("TrackVis Tractography Data Files (*.trk)"));

	if (!filename.isEmpty()) {
		// store filename
		fileType.filename = filename;
		std::string fn = filename.toStdString();

		bool success = false;

		// progress bar and top label
		ui->progressBar->setEnabled(true);
		ui->labelTop->setText("Loading data ...");

		QString filenameWithoutPath = QString::fromStdString(fn.substr(fn.find_last_of("/") + 1));
		std::string filenameExtension = fn.substr(fn.find_last_of(".") + 1);
		if (filenameExtension == "trk") { // TrackVis .trk Tractography track data
			fileType.type = TRK;
			success = loadTRKData(filename);
		}
		else {
			success = false;
			ui->labelTop->setText("Error loading file " + filenameWithoutPath + ": Unknown filename extension.");
		}

		ui->progressBar->setEnabled(false);

		// status message
		if (success) {
			QString type;
			if (fileType.type == TRK) type = "TrackVis Tractography Data";
			ui->labelTop->setText("File LOADED [" + filenameWithoutPath + "], Type [" + type + "]");

			glWidget->initLineRenderMode(&datasetLines);
		}
		else {
			ui->labelTop->setText("ERROR loading file " + filenameWithoutPath + "!");
			ui->progressBar->setValue(0);
		}
	}
}

bool MainWindow::loadTRKData(QString &filename) {

	datasetLines.clear();

	// note we store all lines in a single vector, and separate the ends via a flag set below
	// this makes it easier to render using a single vbo
	std::vector<glm::vec3> linePositions;

	std::string strInputFilePath = filename.toStdString();

	// create reader and open file
	TrkFileReader trkFileReader(strInputFilePath);
	if (!trkFileReader.open())
		return false;

	int numTracks = trkFileReader.getTotalTrkNum(); // number of tractography tracks (lines of traced nerves) in input file
	int numPointsTotal = 0; // total count of points (vertices) in tracks

	// calculate data mean position and bounding box

	glm::vec3 meanPos = glm::vec3(0, 0, 0);
	glm::vec3 boundingBoxMin = glm::vec3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
	glm::vec3 boundingBoxMax = glm::vec3(std::numeric_limits<float>::min(), std::numeric_limits<float>::min(), std::numeric_limits<float>::min());

    for (int trackIndex = 0; trackIndex < numTracks; ++trackIndex) {

		int numPointsInTrack = trkFileReader.getPointNumInTrk(trackIndex);
		numPointsTotal += numPointsInTrack;

        for (int pointIndex = 0; pointIndex < numPointsInTrack; ++pointIndex) {

			std::vector<float> point; // x, y, z
            trkFileReader.readPoint(trackIndex, pointIndex, point);

			meanPos += glm::vec3(point[0], point[2], point[1]); // swap y and z (we use different coords)

			// adjust bounding box to fit track if necessary
			if (point[0] > boundingBoxMax.x) { boundingBoxMax.x = point[0]; }
			if (point[0] < boundingBoxMin.x) { boundingBoxMin.x = point[0]; }
			if (point[2] > boundingBoxMax.y) { boundingBoxMax.y = point[2]; }
			if (point[2] < boundingBoxMin.y) { boundingBoxMin.y = point[2]; }
			if (point[1] > boundingBoxMax.z) { boundingBoxMax.z = point[1]; }
			if (point[1] < boundingBoxMin.z) { boundingBoxMin.z = point[1]; }
        }
    }

	meanPos /= static_cast<float>(numPointsTotal);

	// move bounding box such that mean pos is at origin
	boundingBoxMin -= meanPos;
	boundingBoxMax -= meanPos;

	// find coordinate direction where bounding box has greatest extend
	// used to set view such that everything is visible
	float boundingBoxLengthX = boundingBoxMax.x - boundingBoxMin.x;
	float boundingBoxLengthY = boundingBoxMax.y - boundingBoxMin.y;
	float boundingBoxLengthZ = boundingBoxMax.z - boundingBoxMin.z;
	float boundingBoxLengthMax = glm::max(boundingBoxLengthX, glm::max(boundingBoxLengthY, boundingBoxLengthZ));
	float minValue = 0;
	float maxValue = 0;
	if (boundingBoxLengthMax == boundingBoxLengthX) { minValue = boundingBoxMin.x; maxValue = boundingBoxMax.x; }
	if (boundingBoxLengthMax == boundingBoxLengthY) { minValue = boundingBoxMin.y; maxValue = boundingBoxMax.y; }
	if (boundingBoxLengthMax == boundingBoxLengthZ) { minValue = boundingBoxMin.z; maxValue = boundingBoxMax.z; }

	// for each track read in all points
	for (int trackIndex = 0; trackIndex < numTracks; ++trackIndex) {

		int numPointsInTrack = trkFileReader.getPointNumInTrk(trackIndex); // number of points in current track
		for (int pointIndex = 0; pointIndex < numPointsInTrack; ++pointIndex) {

			std::vector<float> point;
			trkFileReader.readPoint(trackIndex, pointIndex, point);
			glm::vec3 pos = glm::vec3(point[0], point[2], point[1]); // swap y and z (we use different coords)

			// move data such that mean of all data points is at center of coordinate system
			pos -= meanPos;

			// scale data such that largest direction of bounding box is in [-1,1]
			// i.e. map [minValue, maxValue] to [-1,1] or boundingBoxLengthMax to 2
			pos = glm::vec3(-1) + 2.0f * ((pos - glm::vec3(minValue)) / boundingBoxLengthMax);

			// dirty hack: we use this magic value as a flag
			// to discard fragments in the fragment shader that connect end and start vertices of two separate lines
			// this allows us to just use one vbo for all the triangle strip vertices which is much faster.
			if (pointIndex == numPointsInTrack-1)
				pos.z = 42.4242;

			linePositions.push_back(pos);
        }
	}

	// close input file
	trkFileReader.close();

	// adjust draw parameters for this dataset
	ui->spinBoxLineTriangleStripWidth->setValue(0.01f);
	ui->spinBoxLineWidthPercentageBlack->setValue(0.5f);
	ui->spinBoxLineWidthDepthCueingFactor->setValue(0.5f);
	ui->spinBoxLineHaloMaxDepth->setValue(0.04f);

	if (numPointsTotal > 200000) { // big dataset needs other parameters
		ui->spinBoxLineTriangleStripWidth->setValue(0.006f);
		ui->spinBoxLineWidthPercentageBlack->setValue(0.3f);
		ui->spinBoxLineWidthDepthCueingFactor->setValue(1.0f);
		ui->spinBoxLineHaloMaxDepth->setValue(0.04f);
	}

	generateAdditionalLineVertexData(linePositions);

	ui->spinBoxTestDataNumVertices->setValue(datasetLines[0].size());
	qDebug() << "Loaded .trk data with:" << linePositions.size() << "line vertices," << datasetLines[0].size() << "vertices after duplication for triangle strip drawing. Each vertex consists of 8 floats (3 pos, 3 direction to next, 2 uv for triangle strip drawing).";

	return true;
}

void MainWindow::generateAdditionalLineVertexData(std::vector<glm::vec3> linePositions)
{
	// GENERATE ADDITIONAL LINE VERTEX DATA AT LINE POSITIONS (directions and uv)

	std::vector<LineVertex> lineVerticesDoubled;

	glm::vec3 directionToCurrent;
	glm::vec3 directionToNext;

	for (int i = 0; i < linePositions.size(); ++i) {

		LineVertex vertex;
		vertex.pos = linePositions[i];

		// generate vertex data: direction to next vertex
		// take average of direction to current and direction to next for smoother directions
		if (i == 0) { // first element
			directionToCurrent = glm::vec3(0,0,0);
			directionToNext = glm::normalize(linePositions[i+1] - linePositions[i]);
		} else if (i == linePositions.size()-1) { // last element
			directionToCurrent = glm::normalize(linePositions[i] - linePositions[i-1]);
			directionToNext = glm::vec3(0,0,0);
		} else {
			directionToCurrent = glm::normalize(linePositions[i] - linePositions[i-1]);
			directionToNext = glm::normalize(linePositions[i+1] - linePositions[i]);
		}
		vertex.directionToNext = glm::normalize(glm::vec3(directionToCurrent + directionToNext));

		// generate vertex data: uv coordinates to render line as view-aligned triangle strips
		// for this we need two vertices!
		// u-coordinate is same for both and is interpolated along the length of the whole line,
		// v-coordinate is set to 0 for vertex on "right" side of the strip and to 1 for vertex on "left" side.
		float u = ((float)i) / (linePositions.size()-1);
		vertex.uv = glm::vec2(u,0);
		LineVertex vertexCopy = vertex;
		vertexCopy.uv = glm::vec2(u,1);

		// store the vertex and its copy in sequential manner
		lineVerticesDoubled.push_back(vertex);
		lineVerticesDoubled.push_back(vertexCopy);
	}

	datasetLines.push_back(lineVerticesDoubled);
}

void MainWindow::closeAction()
{
	close();
}

void MainWindow::displayTotalGPUMemory(float size)
{
	// size is in mb

	ui->memSizeLCD->display(size);
}
void MainWindow::displayUsedGPUMemory(float size)
{
	// size is in mb

	float total_mem_mb = (float)glWidget->total_mem_kb / 1024;
	if (size > total_mem_mb*0.9)
		ui->usedMemLCD->setPalette(Qt::red);
	else if (size > total_mem_mb*0.75)
		ui->usedMemLCD->setPalette(Qt::yellow);
	else
		ui->usedMemLCD->setPalette(Qt::darkGreen);

	ui->usedMemLCD->display(size);
}

void MainWindow::displayFPS(int fps)
{
	if (fps < 10)
		ui->fpsLCD->setPalette(Qt::red);
	else if (fps < 25)
		ui->fpsLCD->setPalette(Qt::yellow);
	else
		ui->fpsLCD->setPalette(Qt::darkGreen);

	ui->fpsLCD->display(fps);
}

void MainWindow::displayGraphicsDeviceInfo(QString string)
{
	ui->labelGraphicsDeviceInfo->setText(string);
}

void MainWindow::on_generateTestDataButton_clicked()
{
	// the value in the spinBoxTestDataNumVertices should represent the numer of total vertices of the triangle strips
	// we need half of that for the line vertices test data, since line vertices will be duplicated for triangle strip generation
	generateTestData(ui->spinBoxTestDataNumVertices->value()/2, glm::vec3(-1.f,-1.f,-1.f), glm::vec3(1.f,1.f,1.f));
}

void MainWindow::renderModeChanged(int index)
{
	glWidget->renderMode = (GLWidget::RenderMode)index;
	glWidget->update();
}

void MainWindow::on_spinBoxLineTriangleStripWidth_valueChanged(double value)
{
	glWidget->lineTriangleStripWidth = value;
	glWidget->update();
}

void MainWindow::on_spinBoxLineWidthPercentageBlack_valueChanged(double value)
{
	glWidget->lineWidthPercentageBlack = value;
	glWidget->update();
}

void MainWindow::on_spinBoxLineWidthDepthCueingFactor_valueChanged(double value)
{
	glWidget->lineWidthDepthCueingFactor = value;
	glWidget->update();
}

void MainWindow::on_spinBoxLineHaloMaxDepth_valueChanged(double value)
{
	glWidget->lineHaloMaxDepth = value;
	glWidget->update();
}

void MainWindow::on_pushButtonRestoreDefaults_clicked()
{
	ui->spinBoxLineTriangleStripWidth->setValue(0.03f);
	ui->spinBoxLineWidthPercentageBlack->setValue(0.3f);
	ui->spinBoxLineWidthDepthCueingFactor->setValue(1.0f);
	ui->spinBoxLineHaloMaxDepth->setValue(0.02f);

	glWidget->update();
}

void MainWindow::on_checkBoxEnableClipping_clicked(bool checked)
{
	glWidget->enableClipping = checked;
}

void MainWindow::on_pushButtonSetClipPlaneNormal_clicked()
{
	glWidget->updateClipPlaneNormal();
}

void MainWindow::on_horizontalSliderClipPlaneDistance_valueChanged(int value)
{
	glWidget->clipPlaneDistance = (float)value/50.0f - 1.0f; // map [0,100] to [-1,1]
}
