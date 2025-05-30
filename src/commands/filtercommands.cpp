/*
 * Copyright (c) 2021-2025 Meltytech, LLC
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
 */

#include "filtercommands.h"

#include "Logger.h"
#include "controllers/filtercontroller.h"
#include "mainwindow.h"
#include "mltcontroller.h"
#include "qmltypes/qmlapplication.h"

class FindProducerParser : public Mlt::Parser
{
private:
    QUuid m_uuid;
    Mlt::Producer m_producer;

public:
    FindProducerParser(QUuid uuid)
        : Mlt::Parser()
        , m_uuid(uuid)
    {}

    Mlt::Producer producer() { return m_producer; }

    int on_start_filter(Mlt::Filter *) { return 0; }
    int on_start_producer(Mlt::Producer *producer)
    {
        if (MLT.uuid(*producer) == m_uuid) {
            m_producer = producer;
            return 1;
        }
        return 0;
    }
    int on_end_producer(Mlt::Producer *) { return 0; }
    int on_start_playlist(Mlt::Playlist *playlist) { return on_start_producer(playlist); }
    int on_end_playlist(Mlt::Playlist *) { return 0; }
    int on_start_tractor(Mlt::Tractor *tractor) { return on_start_producer(tractor); }
    int on_end_tractor(Mlt::Tractor *) { return 0; }
    int on_start_multitrack(Mlt::Multitrack *) { return 0; }
    int on_end_multitrack(Mlt::Multitrack *) { return 0; }
    int on_start_track() { return 0; }
    int on_end_track() { return 0; }
    int on_end_filter(Mlt::Filter *) { return 0; }
    int on_start_transition(Mlt::Transition *) { return 0; }
    int on_end_transition(Mlt::Transition *) { return 0; }
    int on_start_chain(Mlt::Chain *chain) { return on_start_producer(chain); }
    int on_end_chain(Mlt::Chain *) { return 0; }
    int on_start_link(Mlt::Link *) { return 0; }
    int on_end_link(Mlt::Link *) { return 0; }
};

static Mlt::Producer findProducer(const QUuid &uuid)
{
    FindProducerParser graphParser(uuid);
    if (MAIN.isMultitrackValid()) {
        graphParser.start(*MAIN.multitrack());
        if (graphParser.producer().is_valid()) {
            return graphParser.producer();
        }
    }
    if (MAIN.playlist() && MAIN.playlist()->count() > 0) {
        graphParser.start(*MAIN.playlist());
        if (graphParser.producer().is_valid()) {
            return graphParser.producer();
        }
    }
    Mlt::Producer producer(MLT.isClip() ? MLT.producer() : MLT.savedProducer());
    if (producer.is_valid()) {
        graphParser.start(producer);
        if (graphParser.producer().is_valid()) {
            return graphParser.producer();
        }
    }
    return Mlt::Producer();
}

namespace Filter {

AddCommand::AddCommand(AttachedFiltersModel &model,
                       const QString &name,
                       Mlt::Service &service,
                       int row,
                       AddCommand::AddType type,
                       QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
    , m_producer(*model.producer())
    , m_producerUuid(MLT.ensureHasUuid(m_producer))
    , m_type(type)
{
    if (m_type == AddCommand::AddSingle) {
        setText(QObject::tr("Add %1 filter").arg(name));
    } else {
        setText(QObject::tr("Add %1 filter set").arg(name));
    }
    m_rows.push_back(row);
    m_services.push_back(service);
}

void AddCommand::redo()
{
    LOG_DEBUG() << text() << m_rows[0];
    Mlt::Producer producer = m_producer;
    if (!producer.is_valid()) {
        producer = findProducer(m_producerUuid);
    }
    Q_ASSERT(producer.is_valid());
    int adjustFrom = producer.filter_count();
    for (int i = 0; i < m_rows.size(); i++) {
        m_model.doAddService(producer, m_services[i], m_rows[i]);
    }
    if (AddSetLast == m_type)
        MLT.adjustFilters(producer, adjustFrom);
    // Only hold the producer reference for the first redo and lookup by UUID thereafter.
    m_producer = Mlt::Producer();
}

void AddCommand::undo()
{
    LOG_DEBUG() << text() << m_rows[0];
    Mlt::Producer producer(findProducer(m_producerUuid));
    Q_ASSERT(producer.is_valid());
    // Remove the services in reverse order
    for (int i = m_rows.size() - 1; i >= 0; i--) {
        m_model.doRemoveService(producer, m_rows[i]);
    }
}

bool AddCommand::mergeWith(const QUndoCommand *other)
{
    AddCommand *that = const_cast<AddCommand *>(static_cast<const AddCommand *>(other));
    if (!that || that->id() != id()) {
        LOG_ERROR() << "Invalid merge";
        return false;
    }
    if (m_type != AddSet || !(that->m_type == AddSet || that->m_type == AddSetLast)) {
        // Only merge services from the same filter set
        return false;
    }
    m_type = that->m_type;
    m_rows.push_back(that->m_rows.front());
    m_services.push_back(that->m_services.front());
    return true;
}

RemoveCommand::RemoveCommand(AttachedFiltersModel &model,
                             const QString &name,
                             Mlt::Service &service,
                             int row,
                             QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
    , m_row(row)
    , m_producer(*model.producer())
    , m_producerUuid(MLT.ensureHasUuid(m_producer))
    , m_service(service)
{
    setText(QObject::tr("Remove %1 filter").arg(name));
}

void RemoveCommand::redo()
{
    LOG_DEBUG() << text() << m_row;
    Mlt::Producer producer = m_producer;
    if (!producer.is_valid()) {
        producer = findProducer(m_producerUuid);
    }
    Q_ASSERT(producer.is_valid());
    m_model.doRemoveService(producer, m_row);
    // Only hold the producer reference for the first redo and lookup by UUID thereafter.
    m_producer = Mlt::Producer();
}

void RemoveCommand::undo()
{
    Q_ASSERT(m_service.is_valid());
    LOG_DEBUG() << text() << m_row;
    Mlt::Producer producer(findProducer(m_producerUuid));
    Q_ASSERT(producer.is_valid());
    m_model.doAddService(producer, m_service, m_row);
}

MoveCommand::MoveCommand(
    AttachedFiltersModel &model, const QString &name, int fromRow, int toRow, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
    , m_fromRow(fromRow)
    , m_toRow(toRow)
    , m_producer(*model.producer())
    , m_producerUuid(MLT.ensureHasUuid(m_producer))
{
    setText(QObject::tr("Move %1 filter").arg(name));
}

void MoveCommand::redo()
{
    LOG_DEBUG() << text() << "from" << m_fromRow << "to" << m_toRow;
    Mlt::Producer producer = m_producer;
    if (!producer.is_valid()) {
        producer = findProducer(m_producerUuid);
    }
    Q_ASSERT(producer.is_valid());
    if (producer.is_valid()) {
        m_model.doMoveService(producer, m_fromRow, m_toRow);
    }
    if (m_producer.is_valid()) {
        // Only hold the producer reference for the first redo and lookup by UUID thereafter.
        m_producer = Mlt::Producer();
    }
}

void MoveCommand::undo()
{
    LOG_DEBUG() << text() << "from" << m_toRow << "to" << m_fromRow;
    Mlt::Producer producer(findProducer(m_producerUuid));
    Q_ASSERT(producer.is_valid());
    if (producer.is_valid()) {
        m_model.doMoveService(producer, m_toRow, m_fromRow);
    }
}

DisableCommand::DisableCommand(
    AttachedFiltersModel &model, const QString &name, int row, bool disabled, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
    , m_row(row)
    , m_producer(*model.producer())
    , m_producerUuid(MLT.ensureHasUuid(m_producer))
    , m_disabled(disabled)
{
    if (disabled) {
        setText(QObject::tr("Disable %1 filter").arg(name));
    } else {
        setText(QObject::tr("Enable %1 filter").arg(name));
    }
}

void DisableCommand::redo()
{
    LOG_DEBUG() << text() << m_row;
    Mlt::Producer producer = m_producer;
    if (!producer.is_valid()) {
        producer = findProducer(m_producerUuid);
    }
    Q_ASSERT(producer.is_valid());
    if (producer.is_valid()) {
        m_model.doSetDisabled(producer, m_row, m_disabled);
    }
    if (m_producer.is_valid()) {
        // Only hold the producer reference for the first redo and lookup by UUID thereafter.
        m_producer = Mlt::Producer();
    }
}

void DisableCommand::undo()
{
    LOG_DEBUG() << text() << m_row;
    Mlt::Producer producer(findProducer(m_producerUuid));
    Q_ASSERT(producer.is_valid());
    if (producer.is_valid()) {
        m_model.doSetDisabled(producer, m_row, !m_disabled);
    }
}

bool DisableCommand::mergeWith(const QUndoCommand *other)
{
    // TODO: This doesn't always provide expected results.
    // If you toggle twice and then undo, you get the opposite of the original state.
    // It would make sense to merge three toggles in a row, but not two.
    // Do not implement for now.
    return false;
    /*
        DisableCommand *that = const_cast<DisableCommand *>(static_cast<const DisableCommand *>(other));
        if (!that || that->id() != id())
            return false;
        m_disabled = that->m_disabled;
        setText(that->text());
        return true;
    */
}

PasteCommand::PasteCommand(AttachedFiltersModel &model,
                           const QString &filterProducerXml,
                           QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
    , m_xml(filterProducerXml)
    , m_producerUuid(MLT.ensureHasUuid(*model.producer()))
{
    setText(QObject::tr("Paste filters"));
    m_beforeXml = MLT.XML(model.producer());
}

void PasteCommand::redo()
{
    LOG_DEBUG() << text();
    Mlt::Producer producer = findProducer(m_producerUuid);
    Q_ASSERT(producer.is_valid());
    Mlt::Profile profile(kDefaultMltProfile);
    Mlt::Producer filtersProducer(profile, "xml-string", m_xml.toUtf8().constData());
    if (filtersProducer.is_valid() && filtersProducer.filter_count() > 0) {
        MLT.pasteFilters(&producer, &filtersProducer);
    }
    emit QmlApplication::singleton().filtersPasted(&producer);
}

void PasteCommand::undo()
{
    LOG_DEBUG() << text();
    Mlt::Producer producer = findProducer(m_producerUuid);
    Q_ASSERT(producer.is_valid());
    // Remove all filters
    for (int i = 0; i < producer.filter_count(); i++) {
        Mlt::Filter *filter = producer.filter(i);
        if (filter && filter->is_valid() && !filter->get_int("_loader")
            && !filter->get_int(kShotcutHiddenProperty)) {
            producer.detach(*filter);
            i--;
        }
        delete filter;
    }
    // Restore the "before" filters
    Mlt::Profile profile(kDefaultMltProfile);
    Mlt::Producer filtersProducer(profile, "xml-string", m_beforeXml.toUtf8().constData());
    if (filtersProducer.is_valid() && filtersProducer.filter_count() > 0) {
        MLT.pasteFilters(&producer, &filtersProducer);
    }
    emit QmlApplication::singleton().filtersPasted(&producer);
}

UndoParameterCommand::UndoParameterCommand(const QString &name,
                                           FilterController *controller,
                                           int row,
                                           Mlt::Properties &before,
                                           const QString &desc,
                                           QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_filterController(controller)
    , m_row(row)
    , m_producerUuid(MLT.ensureHasUuid(*controller->attachedModel()->producer()))
    , m_firstRedo(true)
{
    if (desc.isEmpty()) {
        setText(QObject::tr("Change %1 filter").arg(name));
    } else {
        setText(QObject::tr("Change %1 filter: %2").arg(name, desc));
    }
    m_before.inherit(before);
    Mlt::Service *service = controller->attachedModel()->getService(m_row);
    m_after.inherit(*service);
}

void UndoParameterCommand::update(const QString &propertyName)
{
    Mlt::Service *service = m_filterController->attachedModel()->getService(m_row);
    m_after.pass_property(*service, propertyName.toUtf8().constData());
}

void UndoParameterCommand::redo()
{
    LOG_DEBUG() << text();
    if (m_firstRedo) {
        m_firstRedo = false;
    } else {
        Mlt::Producer producer = findProducer(m_producerUuid);
        Q_ASSERT(producer.is_valid());
        if (producer.is_valid() && m_filterController) {
            Mlt::Service service = m_filterController->attachedModel()->doGetService(producer,
                                                                                     m_row);
            service.inherit(m_after);
            m_filterController->onUndoOrRedo(service);
        }
    }
}

void UndoParameterCommand::undo()
{
    LOG_DEBUG() << text();
    Mlt::Producer producer = findProducer(m_producerUuid);
    Q_ASSERT(producer.is_valid());
    if (producer.is_valid() && m_filterController) {
        Mlt::Service service = m_filterController->attachedModel()->doGetService(producer, m_row);
        service.inherit(m_before);
        m_filterController->onUndoOrRedo(service);
    }
}

bool UndoParameterCommand::mergeWith(const QUndoCommand *other)
{
    UndoParameterCommand *that = const_cast<UndoParameterCommand *>(
        static_cast<const UndoParameterCommand *>(other));
    LOG_DEBUG() << "this filter" << m_row << "that filter" << that->m_row;
    if (that->id() != id() || that->m_row != m_row || that->m_producerUuid != m_producerUuid
        || that->text() != text())
        return false;
    m_after = that->m_after;
    return true;
}

} // namespace Filter
