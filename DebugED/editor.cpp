#include "editor.h"
#include "codeed.h"
#include <QtWidgets>
#include <QDebug>

Editor::Editor(CodeED *codeED, QWidget *parent) : QPlainTextEdit(parent), lineWeigth(0){
    _codeED = codeED;
    lineNumberArea = new LineNumberArea(this);

    connect(this, SIGNAL(updateRequest(QRect,int)), this, SLOT(updateLineNumberArea(QRect,int)));
    connect(this, SIGNAL(cursorPositionChanged()), this, SLOT(highlightCurrentLine()));
    if (codeED != NULL)
        connect(_codeED, SIGNAL(passAction(CodeED::Pass)), this, SLOT(passAction(CodeED::Pass)));

    createCompleter();
    highlightCurrentLine();
    setViewportMargins(20, 0, 0, 0);
    setFont(QFont("courier", 10));
    setTabStopWidth(40);
    setTabChangesFocus(false);
    setAutoFillBackground(true);
    setTextInteractionFlags(Qt::TextInteractionFlag::TextEditorInteraction);
}

void Editor::createCompleter(){
    _completer = new QCompleter(this);
    _completer->setModel(modelFromFile(":/complete.txt"));
    _completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
    _completer->setCaseSensitivity(Qt::CaseInsensitive);
    _completer->setWrapAround(false);
    _completer->setWidget(this);
    _completer->setCompletionMode(QCompleter::PopupCompletion);
    _completer->setMaxVisibleItems(10);

    connect(_completer, SIGNAL(activated(QString)),this, SLOT(insertCompletion(QString)));
}

void Editor::insertCompletion(const QString &completion){
    if (_completer->widget() != this)
        return;
    QTextCursor tc = textCursor();
    int extra = completion.length() - _completer->completionPrefix().length();
    tc.movePosition(QTextCursor::Left);
    tc.movePosition(QTextCursor::EndOfWord);
    tc.insertText(completion.right(extra));
    setTextCursor(tc);
}

QAbstractItemModel *Editor::modelFromFile(const QString &fileName){
    QFile file(fileName);
    if (!file.open(QFile::ReadOnly))
        return new QStringListModel(_completer);

    #ifndef QT_NO_CURSOR
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    #endif
    QStringList words;

    while (!file.atEnd()) {
        QByteArray line = file.readLine();
        if (!line.isEmpty())
            words << line.trimmed();
    }

    #ifndef QT_NO_CURSOR
    QApplication::restoreOverrideCursor();
    #endif

    return new QStringListModel(words, _completer);
}

void Editor::updateLineNumberArea(const QRect &rect, int dy){
    if (dy)
        lineNumberArea->scroll(0, dy);
    else
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());

}

void Editor::passAction(CodeED::Pass pass){
    switch(pass){
    case CodeED::Pass::Play:
        moveCursor(QTextCursor::Start);

        break;
    case CodeED::Pass::Next:
        moveCursor(QTextCursor::NextBlock);

        if (!textCursor().movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor)){
            emit blockNext();
        }
        break;
    case CodeED::Pass::Previous:
        moveCursor(QTextCursor::PreviousBlock);

        if (!textCursor().movePosition(QTextCursor::PreviousBlock, QTextCursor::KeepAnchor)){
            emit blockPrevious();
        }
        break;
    case CodeED::Pass::Stop:

        break;
    }

    highlightCurrentLine();
}

void Editor::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);

    QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), 20, cr.height()));
}

void Editor::focusOutEvent(QFocusEvent *e){
    QPlainTextEdit::focusOutEvent(e);
    setExtraSelections(QList<QTextEdit::ExtraSelection>());
}

void Editor::focusInEvent(QFocusEvent *e){
    highlightCurrentLine();

    if (_completer)
        _completer->setWidget(this);

    QPlainTextEdit::focusInEvent(e);
}

void Editor::keyPressEvent(QKeyEvent *e){
    if (_completer && _completer->popup()->isVisible()) {
        // The following keys are forwarded by the completer to the widget
       switch (e->key()) {
       case Qt::Key_Enter:
       case Qt::Key_Return:
       case Qt::Key_Escape:
       case Qt::Key_Tab:
       case Qt::Key_Backtab:
            e->ignore();
            return; // let the completer do default behavior
       default:
           break;
       }
    }

    bool isShortcut = ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_Space); // CTRL+E
    if (!_completer || !isShortcut) // do not process the shortcut when we have a completer
        QPlainTextEdit::keyPressEvent(e);
    //! [7]

    //! [8]
    const bool ctrlOrShift = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    if (!_completer || (ctrlOrShift && e->text().isEmpty()))
        return;

    static QString eow("~!@#$%^&*()_+{}|:\"<>?,./;'[]\\-="); // end of word
    bool hasModifier = (e->modifiers() != Qt::NoModifier) && !ctrlOrShift;

    QTextCursor tc = textCursor();
    tc.select(QTextCursor::LineUnderCursor);
    QString completionPrefix = tc.selectedText();

    if (!isShortcut && (hasModifier || e->text().isEmpty()|| completionPrefix.length() < 0
                      || eow.contains(e->text().right(1)))) {
        _completer->popup()->hide();
        return;
    }

    if (completionPrefix != _completer->completionPrefix()) {
        _completer->setCompletionPrefix(completionPrefix);
        _completer->popup()->setCurrentIndex(_completer->completionModel()->index(0, 0));
    }

    QRect cr = cursorRect();
    cr.setWidth(_completer->popup()->sizeHintForColumn(0)
                + _completer->popup()->verticalScrollBar()->sizeHint().width());
    _completer->complete(cr); // popup it up!
}

void Editor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> extraSelections;

    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;

        QColor lineColor = QColor(Qt::yellow).lighter(160);

        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();

        lineWeigth = selection.cursor.blockNumber();
        extraSelections.append(selection);


    }

    setExtraSelections(extraSelections);
}

void Editor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(lineNumberArea);
    painter.fillRect(event->rect(), QColor(Qt::lightGray).lighter(120));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = (int) blockBoundingGeometry(block).translated(contentOffset()).top();
    int bottom = top + (int) blockBoundingRect(block).height();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {

            QString number = QString::number(blockNumber + 1);
            painter.setPen(QColor(Qt::darkCyan).light(150));
            if (lineWeigth == blockNumber){
                painter.setFont(QFont("Courier", 10, QFont::Black));
            }else{
                painter.setFont(QFont("courier", 10));
            }
            painter.drawText(0, top, lineNumberArea->width(), fontMetrics().height(),
                             Qt::AlignRight, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + (int) blockBoundingRect(block).height();
        ++blockNumber;
    }
}
