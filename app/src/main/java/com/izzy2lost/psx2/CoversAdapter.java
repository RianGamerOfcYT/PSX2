package com.izzy2lost.psx2;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.bumptech.glide.Glide;
import com.bumptech.glide.load.engine.DiskCacheStrategy;
import java.io.File;

public class CoversAdapter extends RecyclerView.Adapter<CoversAdapter.VH> {
    public interface OnItemClick {
        void onClick(int position);
    }

    public interface OnItemLongClick {
        void onLongClick(int position);
    }

    private final Context context;
    private final String[] titles;
    private final String[] coverUrls;
    private final String[] localPaths; // absolute file paths for cached covers (may be null)
    private final OnItemClick onItemClick;
    private final OnItemLongClick onItemLongClick;
    private final int itemLayoutResId;
    private int overrideItemWidthPx = 0;

    public CoversAdapter(Context context, String[] titles, String[] coverUrls, String[] localPaths, OnItemClick click) {
        this(context, titles, coverUrls, localPaths, R.layout.item_cover, click, null);
    }

    public CoversAdapter(Context context, String[] titles, String[] coverUrls, String[] localPaths, int itemLayoutResId, OnItemClick click, OnItemLongClick longClick) {
        this.context = context;
        this.titles = titles;
        this.coverUrls = coverUrls;
        this.localPaths = localPaths;
        this.itemLayoutResId = itemLayoutResId;
        this.onItemClick = click;
        this.onItemLongClick = longClick;
        setHasStableIds(true);
    }

    public void setItemWidthPx(int widthPx) {
        if (widthPx != overrideItemWidthPx) {
            overrideItemWidthPx = widthPx;
            notifyDataSetChanged();
        }
    }

    @NonNull
    @Override
    public VH onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View v = LayoutInflater.from(parent.getContext()).inflate(itemLayoutResId, parent, false);
        if (overrideItemWidthPx > 0) {
            RecyclerView.LayoutParams lp = (RecyclerView.LayoutParams) v.getLayoutParams();
            lp.width = overrideItemWidthPx;
            v.setLayoutParams(lp);
        }
        return new VH(v);
    }

    @Override
    public void onBindViewHolder(@NonNull VH holder, int position) {
        int count = titles.length;
        int real = (count == 0) ? 0 : (position % count);
        // Ensure dynamic width is applied
        if (overrideItemWidthPx > 0) {
            RecyclerView.LayoutParams lp = (RecyclerView.LayoutParams) holder.itemView.getLayoutParams();
            if (lp.width != overrideItemWidthPx) {
                lp.width = overrideItemWidthPx;
                holder.itemView.setLayoutParams(lp);
            }
        }
        holder.title.setText(titles[real]);
        String local = (localPaths != null && real < localPaths.length) ? localPaths[real] : null;
        File localFile = null;
        if (local != null) {
            File f = new File(local);
            if (f.exists() && f.length() > 0) localFile = f;
        }

        if (localFile != null) {
            Glide.with(context)
                    .load(localFile)
                    .diskCacheStrategy(DiskCacheStrategy.AUTOMATIC)
                    .fitCenter()
                    .placeholder(android.R.color.transparent)
                    .error(android.R.color.transparent)
                    .into(holder.cover);
        } else {
            // Show a default placeholder from resources/no-cover.png if present
            File resDir = context.getExternalFilesDir("resources");
            File placeholder = (resDir != null) ? new File(resDir, "no-cover.png") : null;
            if (placeholder != null && placeholder.exists() && placeholder.length() > 0) {
                Glide.with(context)
                        .load(placeholder)
                        .diskCacheStrategy(DiskCacheStrategy.AUTOMATIC)
                        .fitCenter()
                        .placeholder(android.R.color.transparent)
                        .error(android.R.color.transparent)
                        .into(holder.cover);
            } else {
                // Fallback to logo if placeholder not found
                holder.cover.setImageResource(R.drawable.psx2_logo2_fixed);
            }
        }
        holder.itemView.setOnClickListener(v -> {
            if (onItemClick != null) onItemClick.onClick(real);
        });
        holder.itemView.setOnLongClickListener(v -> {
            if (onItemLongClick != null) {
                onItemLongClick.onLongClick(real);
                return true;
            }
            return false;
        });
    }

    @Override
    public int getItemCount() {
        return titles.length == 0 ? 0 : Integer.MAX_VALUE;
    }

    @Override
    public long getItemId(int position) {
        if (titles.length == 0) return RecyclerView.NO_ID;
        int real = position % titles.length;
        String t = titles[real];
        return t != null ? t.hashCode() : real;
    }

    static class VH extends RecyclerView.ViewHolder {
        final ImageView cover;
        final TextView title;
        VH(@NonNull View itemView) {
            super(itemView);
            cover = itemView.findViewById(R.id.image_cover);
            title = itemView.findViewById(R.id.text_title);
        }
    }
}
