from flask import Flask, render_template, request, redirect, url_for, session, jsonify
from flask_socketio import SocketIO, emit, join_room, leave_room
import random
import time
import my_engine
import datetime


app = Flask(__name__)
app.config['SECRET_KEY'] = 'your_secret_key_for_socketio'
app.secret_key = 'your_main_app_secret_key'

# Note: Using eventlet is a good choice for production
socketio = SocketIO(app, cors_allowed_origins="*")

banana_book = my_engine.OrderBook()
apple_book = my_engine.OrderBook()

users = {
    "testuser": "password123",
    "admin": "adminpass",
    "alice": "alicepass",
    "bob": "bobpass"
}

# Price history for each fruit
price_history = {
    'apple': [],
    'banana': []
}

# User balances (username: balance)
user_balances = {
    "testuser": 10000,
    "admin": 10000,
    "alice": 10000,
    "bob": 10000,
    "system": 0  # for initial demo orders
}

# Store user orders: {username: {fruit_name: [order_dict, ...]}}
user_orders = {}

# Map order_id to (username, fruit_name, order_type)
order_owners = {}

# --- NEW: track user positions (shares) per fruit ---
# e.g. user_positions['alice']['apple'] = number of apple shares available (not reserved)
user_positions = {
    "testuser": {"apple": 100, "banana": 100},
    "admin": {"apple": 100, "banana": 100},
    "alice": {"apple": 100, "banana": 100},
    "bob": {"apple": 100, "banana": 100},
    "system": {"apple": 1000, "banana": 1000}
}

# --- NEW: server-side terminal history and broadcast helper ---
terminal_history = []
TERMINAL_HISTORY_MAX = 200

def add_to_terminal_history(msg: str):
    terminal_history.append(msg)
    if len(terminal_history) > TERMINAL_HISTORY_MAX:
        # drop oldest
        del terminal_history[0: len(terminal_history) - TERMINAL_HISTORY_MAX]

def broadcast_terminal(msg: str, room: str = None):
    """Store msg in history and emit to socket clients.
    If room is provided emit to that room, otherwise broadcast to all."""
    add_to_terminal_history(msg)
    try:
        if room:
            socketio.emit('terminal_output', {'data': msg}, room=room)
        else:
            socketio.emit('terminal_output', {'data': msg}, broadcast=True)
    except Exception:
        # swallow socket errors to avoid crashing the app
        pass

# --- Add initial orders and price history for easier chart testing ---
def initialize_books_and_history():
    # Add some initial orders to apple_book
    my_engine.matching_engine_process(apple_book, 1001, 100, 10, True)  # Bid
    my_engine.matching_engine_process(apple_book, 1002, 98, 15, True)   # Bid
    my_engine.matching_engine_process(apple_book, 2001, 105, 8, False)  # Ask
    my_engine.matching_engine_process(apple_book, 2002, 110, 12, False) # Ask

    # Add some initial orders to banana_book
    my_engine.matching_engine_process(banana_book, 3001, 50, 20, True)  # Bid
    my_engine.matching_engine_process(banana_book, 3002, 48, 10, True)  # Bid
    my_engine.matching_engine_process(banana_book, 4001, 55, 18, False) # Ask
    my_engine.matching_engine_process(banana_book, 4002, 60, 5, False)  # Ask

    # We'll build a small synthetic historical series first to make chart look real.
    now = datetime.datetime.now()
    # synthetic history: 10 points spaced 30s apart
    def add_synthetic(fruit, base_price):
        hist = []
        for i in range(10, 0, -1):
            t = (now - datetime.timedelta(seconds=i * 30)).isoformat()
            bid = max(1, base_price + random.randint(-4, 4))
            ask = bid + random.randint(1, 5)
            hist.append({'bid': bid, 'ask': ask, 'timestamp': t})
        # attach to global price_history
        price_history[fruit].extend(hist)

    add_synthetic('apple', 100)
    add_synthetic('banana', 50)

    # Then add actual initial orders into the book (these may affect best bid/ask)
    my_engine.matching_engine_process(apple_book, 1001, 100, 10, True)  # Bid
    my_engine.matching_engine_process(apple_book, 1002, 98, 15, True)   # Bid
    my_engine.matching_engine_process(apple_book, 2001, 105, 8, False)  # Ask
    my_engine.matching_engine_process(apple_book, 2002, 110, 12, False) # Ask

    my_engine.matching_engine_process(banana_book, 3001, 50, 20, True)  # Bid
    my_engine.matching_engine_process(banana_book, 3002, 48, 10, True)  # Bid
    my_engine.matching_engine_process(banana_book, 4001, 55, 18, False) # Ask
    my_engine.matching_engine_process(banana_book, 4002, 60, 5, False)  # Ask

    # Now append a "current" point
    now_iso = now.isoformat()
    price_history['apple'].append({
        'bid': apple_book.get_best_bid_price(),
        'ask': apple_book.get_best_ask_price(),
        'timestamp': now_iso
    })
    price_history['banana'].append({
        'bid': banana_book.get_best_bid_price(),
        'ask': banana_book.get_best_ask_price(),
        'timestamp': now_iso
    })

    # Add initial orders as 'system' user for demo and register them
    for fruit, book, prefix in [
        ('apple', apple_book, 1000),
        ('banana', banana_book, 3000)
    ]:
        user_orders.setdefault('system', {}).setdefault(fruit, [])
        # ensure system has large positions for asks
        user_positions.setdefault('system', {}).setdefault(fruit, 1000)
        for i, (oid, price, qty, is_bid) in enumerate([
            (prefix+1, 100 if fruit=='apple' else 50, 10 if fruit=='apple' else 20, True),
            (prefix+2, 98 if fruit=='apple' else 48, 15 if fruit=='apple' else 10, True),
            (prefix+101, 105 if fruit=='apple' else 55, 8 if fruit=='apple' else 18, False),
            (prefix+102, 110 if fruit=='apple' else 60, 12 if fruit=='apple' else 5, False),
        ]):
            # Add to C++ book
            my_engine.matching_engine_process(book, oid, price, qty, is_bid)
            # Register as system orders (resting)
            user_orders['system'][fruit].append({
                'order_id': oid,
                'price': price,
                'quantity': qty,
                'side': 'Bid' if is_bid else 'Ask'
            })
            order_owners[oid] = ('system', fruit, 'bid' if is_bid else 'ask')
            # If we registered an ask, reduce system positions (reserve)
            if not is_bid:
                user_positions['system'][fruit] = user_positions['system'].get(fruit, 0) - qty

    # Broadcast initialization summary so connected clients can see it
    broadcast_terminal("System orders initialized for demo.")

# Call initialization at startup
initialize_books_and_history()

# --- Helpers to keep user_orders/order_owners/positions in sync ---

def update_user_order_quantity(order_id, new_quantity):
    owner_info = order_owners.get(order_id)
    if not owner_info:
        return
    owner, fruit, order_type = owner_info
    orders = user_orders.get(owner, {}).get(fruit, [])
    for od in orders:
        if od['order_id'] == order_id:
            od['quantity'] = new_quantity
            return

def remove_user_order(order_id, cancel=False, refund_unused_bid=True):
    """
    Remove order ownership record and update user lists/positions/balances.
    cancel=True means the order is being cancelled -> refund reserved funds/shares.
    cancel=False means the order was filled -> do not refund reserved shares/funds.
    """
    owner_info = order_owners.get(order_id)
    if not owner_info:
        return
    owner, fruit, order_type = owner_info
    orders = user_orders.get(owner, {}).get(fruit, [])
    for idx, od in enumerate(list(orders)):
        if od['order_id'] == order_id:
            # If cancel: refund unused resources
            if cancel:
                if order_type == 'bid':
                    # refund remaining reserved cash
                    user_balances[owner] += od['quantity'] * od['price']
                else:  # ask
                    # return remaining reserved shares
                    user_positions.setdefault(owner, {}).setdefault(fruit, 0)
                    user_positions[owner][fruit] += od['quantity']
            # Remove from list
            orders.remove(od)
            break
    # remove ownership entry
    order_owners.pop(order_id, None)

# --- Modified matching wrapper that uses inspectors and updates user data ---
def process_order_with_ownership(book, order_id, price, quantity, is_bid, username, fruit_name):
    # Reserve funds or shares on order placement
    # For bids: check/hold balance
    if is_bid:
        total_cost = price * quantity
        if user_balances.get(username, 0) < total_cost:
            return False
        # hold funds immediately
        user_balances[username] -= total_cost
    else:
        # For asks: ensure user has enough shares and reserve them
        user_positions.setdefault(username, {}).setdefault(fruit_name, 0)
        if user_positions[username].get(fruit_name, 0) < quantity:
            return False
        user_positions[username][fruit_name] -= quantity  # reserve shares

    orig_quantity = quantity

    # Aggressive BID incoming
    if is_bid:
        while quantity > 0 and price >= book.get_best_ask_price():
            best_ask_price = book.get_best_ask_price()
            if best_ask_price == 2**32 - 1:
                break
            matched_order_id = book.get_price_level_head_order_id(best_ask_price, False)
            if not matched_order_id:
                break
            resting_qty = book.get_order_quantity(matched_order_id)
            if resting_qty == 0:
                break
            trade_quantity = min(quantity, resting_qty)

            # Broadcast trade event
            broadcast_terminal(f"TRADE: {trade_quantity} @ {best_ask_price} between incoming {order_id} (buyer: {username}) and resting {matched_order_id}")

            matched_owner_info = order_owners.get(matched_order_id)
            # If resting ask is fully filled -> remove ownership (filled, cancel=False)
            if resting_qty == trade_quantity:
                remove_user_order(matched_order_id, cancel=False)
                # announce removal
                if matched_owner_info:
                    broadcast_terminal(f"Order {matched_order_id} (owner: {matched_owner_info[0]}) fully filled and removed")
            # Credit the ASK owner with trade proceeds (buyer funds were reserved earlier)
            if matched_owner_info and matched_owner_info[2] == 'ask':
                owner = matched_owner_info[0]
                user_balances[owner] = user_balances.get(owner, 0) + (trade_quantity * best_ask_price)

            # --- NEW: credit incoming buyer with shares ---
            user_positions.setdefault(username, {}).setdefault(fruit_name, 0)
            user_positions[username][fruit_name] += trade_quantity

            # Update quantities
            quantity -= trade_quantity
            if resting_qty == trade_quantity:
                book.cancel_order(matched_order_id)
            else:
                new_qty = resting_qty - trade_quantity
                book.modify_order(matched_order_id, new_qty)
                update_user_order_quantity(matched_order_id, new_qty)
        # leftover becomes resting bid
        if quantity > 0:
            book.add_order(order_id, price, quantity, True)
            user_orders.setdefault(username, {}).setdefault(fruit_name, []).append({
                'order_id': order_id,
                'price': price,
                'quantity': quantity,
                'side': 'Bid'
            })
            order_owners[order_id] = (username, fruit_name, 'bid')
            # notify all users that someone placed a resting bid
            broadcast_terminal(f"User '{username}' placed BID {order_id}: {quantity} @ {price} ({fruit_name})")
    # Aggressive ASK incoming
    else:
        while quantity > 0 and price <= book.get_best_bid_price():
            best_bid_price = book.get_best_bid_price()
            if best_bid_price == 0:
                break
            matched_order_id = book.get_price_level_head_order_id(best_bid_price, True)
            if not matched_order_id:
                break
            resting_qty = book.get_order_quantity(matched_order_id)
            if resting_qty == 0:
                break
            trade_quantity = min(quantity, resting_qty)

            # Broadcast trade event
            broadcast_terminal(f"TRADE: {trade_quantity} @ {best_bid_price} between incoming {order_id} (seller: {username}) and resting {matched_order_id}")

            matched_owner_info = order_owners.get(matched_order_id)
            # If resting bid is fully filled, remove it (filled)
            if resting_qty == trade_quantity:
                remove_user_order(matched_order_id, cancel=False, refund_unused_bid=False)
                try:
                    if matched_owner_info:
                        broadcast_terminal(f"Order {matched_order_id} (owner: {matched_owner_info[0]}) fully filled and removed")
                except Exception:
                    pass
            # Seller (incoming ask) receives proceeds:
            user_balances[username] = user_balances.get(username, 0) + (trade_quantity * best_bid_price)

            # --- NEW: credit the resting bid owner (buyer) with shares ---
            if matched_owner_info and matched_owner_info[2] == 'bid':
                buyer = matched_owner_info[0]
                buyer_fruit = matched_owner_info[1]
                user_positions.setdefault(buyer, {}).setdefault(buyer_fruit, 0)
                user_positions[buyer][buyer_fruit] += trade_quantity

            quantity -= trade_quantity
            if resting_qty == trade_quantity:
                book.cancel_order(matched_order_id)
            else:
                new_qty = resting_qty - trade_quantity
                book.modify_order(matched_order_id, new_qty)
                update_user_order_quantity(matched_order_id, new_qty)
        # remaining quantity becomes resting ask (shares already reserved)
        if quantity > 0:
            book.add_order(order_id, price, quantity, False)
            user_orders.setdefault(username, {}).setdefault(fruit_name, []).append({
                'order_id': order_id,
                'price': price,
                'quantity': quantity,
                'side': 'Ask'
            })
            order_owners[order_id] = (username, fruit_name, 'ask')
            try:
                socketio.emit('terminal_output', {'data': f"User '{username}' placed ASK {order_id}: {quantity} @ {price} ({fruit_name})"}, broadcast=True)
            except Exception:
                pass

    return True

@app.route('/')
def index():
    if 'username' in session:
        return redirect(url_for('fruit_page', fruit_name='apple'))
    return redirect(url_for('login'))

@app.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        username = request.form['username']
        password = request.form['password']
        if username in users and users[username] == password:
            session['username'] = username
            return redirect(url_for('fruit_page', fruit_name='apple'))
        else:
            return render_template('login.html', error='Invalid credentials')
    return render_template('login.html')

@app.route('/fruits/<fruit_name>', methods=['GET', 'POST'])
def fruit_page(fruit_name):
    if 'username' not in session:
        return redirect(url_for('login'))

    fruit_data = {
        'apple': {
            'name': 'Apple',
            'color': 'Red or Green',
            'description': 'A crisp, sweet, or sour fruit. Great for eating raw or in pies.',
        },
        'banana': {
            'name': 'Banana',
            'color': 'Yellow',
            'description': 'A long, curved fruit which grows in bunches. Very nutritious and easy to peel.',
        }
    }

    book = apple_book if fruit_name.lower() == 'apple' else banana_book

    fruit = fruit_data.get(fruit_name.lower())

    if not fruit:
        return redirect(url_for('fruit_page', fruit_name='apple'))

    username = session['username']
    user_orders.setdefault(username, {}).setdefault(fruit_name.lower(), [])

    # Read & clear any messages left in session (Post-Redirect-Get)
    form_message = session.pop('form_message', None)
    form_error = session.pop('form_error', None)

    if request.method == 'POST':
        price_str = request.form.get('price')
        quantity_str = request.form.get('quantity')
        order_type = request.form.get('order_type')

        try:
            price = int(price_str)
            quantity = int(quantity_str)
            is_bid = order_type == 'bid'

            if not (0 <= price <= 1000):
                form_error = "Server: Price must be between 0 and 1000."
            elif quantity <= 0:
                form_error = "Server: Quantity must be a positive whole number."
            elif order_type not in ['bid', 'ask']:
                form_error = "Server: Invalid order type."

        except (ValueError, TypeError):
            form_error = "Server: Invalid price or quantity format."

        if not form_error:
            order_id = int(time.time() * 1000)
            ok = process_order_with_ownership(
                book, order_id, price, quantity, is_bid, username, fruit_name.lower()
            )
            if not ok and is_bid:
                form_error = "Insufficient balance for this bid order."
            elif not ok and not is_bid:
                form_error = "Insufficient shares to place this ask order."
            else:
                # Update price history after transaction
                best_bid = book.get_best_bid_price()
                best_ask = book.get_best_ask_price()
                price_history[fruit_name.lower()].append({
                    'bid': best_bid if best_bid > 0 else None,
                    'ask': best_ask if best_ask != 2**32 - 1 else None,
                    'timestamp': datetime.datetime.now().isoformat()
                })
                # Limit history to last 30 transactions
                if len(price_history[fruit_name.lower()]) > 30:
                    price_history[fruit_name.lower()] = price_history[fruit_name.lower()][-30:]
                terminal_message = (
                    f"User '{username}' submitted order for {fruit['name']}: "
                    f"Type='{order_type.upper()}', Price={price:.2f}, Quantity={quantity} at {time.ctime()}"
                )
                # emit to the user, and broadcast to all (store history)
                try:
                    socketio.emit('terminal_output', {'data': terminal_message}, room=username)
                except Exception:
                    pass
                broadcast_terminal(terminal_message)
                # Use session + redirect to avoid re-submission on refresh
                session['form_message'] = "Order submitted successfully!"
                return redirect(url_for('fruit_page', fruit_name=fruit_name))
        # If validation or processing produced an error, store it and redirect (so refresh won't resubmit)
        if form_error:
            try:
                socketio.emit('terminal_output', {'data': f"Order submission failed: {form_error}"}, room=username)
                socketio.emit('terminal_output', {'data': f"Order submission failed for {username}: {form_error}"}, broadcast=True)
            except Exception:
                pass
            session['form_error'] = form_error
            return redirect(url_for('fruit_page', fruit_name=fruit_name))

    # Get best bid and ask from the book
    best_bid = book.get_best_bid_price()
    best_ask = book.get_best_ask_price()
    # If no bids/asks, show as "N/A"
    best_bid_display = best_bid if best_bid > 0 else "N/A"
    best_ask_display = best_ask if best_ask < 2**32 - 1 else "N/A"

    print("DEBUG: best_ask =", best_ask)  # Debugging line to check best_ask value

    initial_terminal_message = f"Backend processed request for '{fruit['name']}' at {time.ctime()}"
    socketio.emit('terminal_output', {'data': initial_terminal_message}, room=username)

    # Get user's orders for this fruit
    user_orders_list = user_orders.get(username, {}).get(fruit_name.lower(), [])

    # Get user's balance and position
    balance = user_balances.get(username, 0)
    position = user_positions.get(username, {}).get(fruit_name.lower(), 0)

    return render_template(
        'fruit_page.html',
        username=username,
        fruit=fruit,
        form_message=form_message,
        form_error=form_error,
        fruit_name=fruit_name,
        best_bid=best_bid_display,
        best_ask=best_ask_display,
        price_history=price_history[fruit_name.lower()],
        user_orders=user_orders_list,
        balance=balance,
        position=position
    ) 

@app.route('/get_dynamic_number')
def get_dynamic_number():
    global current_dynamic_number
    current_dynamic_number += random.randint(1, 5)
    if current_dynamic_number > 100:
        current_dynamic_number = 0
    return jsonify(number=current_dynamic_number)

@app.route('/logout')
def logout():
    session.pop('username', None)
    return redirect(url_for('login'))

@socketio.on('connect')
def test_connect():
    if 'username' in session:
        print(f"Client {request.sid} connected for user {session['username']}")
        join_room(session['username'])
        # 'emit' is correct here because it's inside a SocketIO event handler
        emit('terminal_output', {'data': f"Welcome {session['username']}! Connected to the backend terminal."}, room=session['username'])
    else:
        print(f"Unauthorized client {request.sid} tried to connect to SocketIO")
        return False

@socketio.on('disconnect')
def test_disconnect():
    if 'username' in session:
        print(f"Client {request.sid} disconnected for user {session['username']}")
        leave_room(session['username'])
    else:
        print(f"Client {request.sid} disconnected (unauthenticated)")

@app.route('/price_history/<fruit_name>')
def get_price_history(fruit_name):
    return jsonify(price_history.get(fruit_name.lower(), []))

# --- NEW: endpoint to fetch recent terminal history ---
@app.route('/terminal_history')
def get_terminal_history():
    return jsonify(terminal_history)

if __name__ == '__main__':
    socketio.run(app, debug=True)