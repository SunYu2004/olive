/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2020 Olive Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "widget/timelinewidget/timelinewidget.h"

#include "node/block/gap/gap.h"
#include "ripple.h"
#include "widget/nodeview/nodeviewundo.h"

namespace olive {

RippleTool::RippleTool(TimelineWidget* parent) :
  PointerTool(parent)
{
  SetMovementAllowed(false);
  SetGapTrimmingAllowed(true);
}

void RippleTool::InitiateDrag(Block *clicked_item,
                              Timeline::MovementMode trim_mode)
{
  InitiateDragInternal(clicked_item, trim_mode, true, true, false);

  if (!parent()->HasGhosts()) {
    return;
  }

  // Find the earliest ripple
  rational earliest_ripple = RATIONAL_MAX;

  foreach (TimelineViewGhostItem* ghost, parent()->GetGhostItems()) {
    rational ghost_ripple_point;

    if (trim_mode == Timeline::kTrimIn) {
      ghost_ripple_point = ghost->GetIn();
    } else {
      ghost_ripple_point = ghost->GetOut();
    }

    earliest_ripple = qMin(earliest_ripple, ghost_ripple_point);
  }

  // For each track that does NOT have a ghost, we need to make one for Gaps
  foreach (Track* track, parent()->GetConnectedNode()->GetTracks()) {
    if (track->IsLocked()) {
      continue;
    }

    // Determine if we've already created a ghost on this track
    bool ghost_on_this_track_exists = false;

    foreach (TimelineViewGhostItem* ghost, parent()->GetGhostItems()) {
      if (parent()->GetTrackFromReference(ghost->GetTrack()) == track) {
        ghost_on_this_track_exists = true;
        break;
      }
    }

    // If there's no ghost on this track, create one
    if (!ghost_on_this_track_exists) {
      // Find the block that starts just before the ripple point, and ends either on or just after it
      Block* block_before_ripple = track->NearestBlockBefore(earliest_ripple);

      // If block is null, there will be no blocks after to ripple
      if (block_before_ripple) {
        TimelineViewGhostItem* ghost;

        if (block_before_ripple->type() == Block::kGap) {
          // If this Block is already a Gap, ghost it now
          ghost = AddGhostFromBlock(block_before_ripple, trim_mode);
        } else if (block_before_ripple->next()) {
          // Assuming this block is NOT at the end of the track (i.e. next != null)

          // We're going to create a gap after it. If next is a gap, we can just use that
          if (block_before_ripple->next()->type() == Block::kGap) {
            ghost = AddGhostFromBlock(block_before_ripple->next(), trim_mode);
          } else {
            // If next is NOT a gap, we'll need to create one, for which we'll use a null ghost
            ghost = AddGhostFromNull(block_before_ripple->out(), block_before_ripple->out(), track->ToReference(), trim_mode);
            ghost->SetData(TimelineViewGhostItem::kReferenceBlock, Node::PtrToValue(block_before_ripple));
          }
        }
      }
    }
  }
}

void RippleTool::FinishDrag(TimelineViewMouseEvent *event)
{
  Q_UNUSED(event)

  if (parent()->HasGhosts()) {
    QVector< QHash<Track*, TrackListRippleToolCommand::RippleInfo> > info_list(Track::kCount);

    foreach (TimelineViewGhostItem* ghost, parent()->GetGhostItems()) {
      if (!ghost->HasBeenAdjusted()) {
        continue;
      }

      Track* track = parent()->GetTrackFromReference(ghost->GetTrack());

      TrackListRippleToolCommand::RippleInfo info;
      Block* b = Node::ValueToPtr<Block>(ghost->GetData(TimelineViewGhostItem::kAttachedBlock));

      if (b) {
        info.block = b;
        info.append_gap = false;
      } else {
        info.block = Node::ValueToPtr<Block>(ghost->GetData(TimelineViewGhostItem::kReferenceBlock));
        info.append_gap = true;
      }

      info_list[track->type()].insert(track, info);
    }

    QUndoCommand* command = new QUndoCommand();

    rational movement;

    if (drag_movement_mode() == Timeline::kTrimOut) {
      movement = parent()->GetGhostItems().first()->GetOutAdjustment();
    } else {
      movement = parent()->GetGhostItems().first()->GetInAdjustment();
    }

    for (int i=0;i<info_list.size();i++) {
      if (!info_list.at(i).isEmpty()) {
        new TrackListRippleToolCommand(parent()->GetConnectedNode()->track_list(static_cast<Track::Type>(i)),
                                       info_list.at(i),
                                       movement,
                                       drag_movement_mode(),
                                       command);
      }
    }

    if (command->childCount() > 0) {
      TimelineWidgetSelections new_sel = parent()->GetSelections();
      TimelineViewGhostItem* reference_ghost = parent()->GetGhostItems().first();
      if (drag_movement_mode() == Timeline::kTrimIn) {
        new_sel.TrimOut(-reference_ghost->GetInAdjustment());
      } else {
        new_sel.TrimOut(reference_ghost->GetOutAdjustment());
      }
      new TimelineWidget::SetSelectionsCommand(parent(), new_sel, parent()->GetSelections(), command);

      Core::instance()->undo_stack()->push(command);
    } else {
      delete command;
    }
  }
}

}
